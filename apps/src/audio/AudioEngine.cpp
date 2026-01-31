#include "AudioEngine.h"
#include "core/LoggingChannels.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace DirtSim {
namespace AudioProcess {
namespace {

std::vector<std::string> listOutputDevices()
{
    std::vector<std::string> devices;
    const int count = SDL_GetNumAudioDevices(0);
    if (count <= 0) {
        return devices;
    }

    devices.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 0);
        if (name) {
            devices.emplace_back(name);
        }
    }
    return devices;
}

} // namespace

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    stop();
}

Result<std::monostate, ApiError> AudioEngine::start(const AudioEngineConfig& config)
{
    config_ = config;

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            return Result<std::monostate, ApiError>::error(
                ApiError(std::string("SDL audio init failed: ") + SDL_GetError()));
        }
        sdlInitialized_ = true;
    }

    SDL_AudioSpec desired{};
    desired.freq = config_.sampleRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = static_cast<Uint8>(std::clamp(config_.channels, 1, 2));
    desired.samples = static_cast<Uint16>(std::clamp(config_.bufferFrames, 128, 4096));
    desired.callback = &AudioEngine::audioCallback;
    desired.userdata = this;

    SDL_AudioSpec obtained{};
    const char* deviceName = config_.deviceName.empty() ? nullptr : config_.deviceName.c_str();
    const int allowedChanges = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE
        | SDL_AUDIO_ALLOW_FORMAT_CHANGE;
    const int maxAttempts = config_.deviceName.empty() ? 50 : 1;
    const char* currentDriver = SDL_GetCurrentAudioDriver();
    if (currentDriver) {
        SLOG_INFO("Audio driver: {}", currentDriver);
    }
    else {
        SLOG_WARN("Audio driver not available");
    }

    std::string openError;
    auto openWithRetries =
        [&](const char* name, SDL_AudioSpec* outSpec, int attempts) -> SDL_AudioDeviceID {
        SDL_AudioDeviceID deviceId = 0;
        for (int attempt = 0; attempt < attempts && deviceId == 0; ++attempt) {
            deviceId = SDL_OpenAudioDevice(name, 0, &desired, outSpec, allowedChanges);
            if (deviceId == 0) {
                openError = SDL_GetError();
                if (attempt + 1 < attempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            }
        }
        return deviceId;
    };

    deviceId_ = openWithRetries(deviceName, &obtained, maxAttempts);
    if (deviceId_ == 0 && config_.deviceName.empty()) {
        const auto devices = listOutputDevices();
        if (devices.empty()) {
            SLOG_WARN("No SDL audio output devices reported");
        }
        else {
            for (const auto& name : devices) {
                SDL_AudioSpec probe{};
                const SDL_AudioDeviceID probeId = openWithRetries(name.c_str(), &probe, 1);
                if (probeId != 0) {
                    deviceId_ = probeId;
                    obtained = probe;
                    config_.deviceName = name;
                    break;
                }
                SLOG_WARN("Audio device '{}' open failed: {}", name, openError);
            }
        }
    }

    if (deviceId_ == 0 && config_.deviceName.empty()) {
        const std::string fallbackError = openError.empty() ? SDL_GetError() : openError;
        if (sdlInitialized_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            sdlInitialized_ = false;
        }

        if (SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0
            && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
            sdlInitialized_ = true;
            SDL_AudioSpec fallbackObtained{};
            deviceId_ =
                SDL_OpenAudioDevice(nullptr, 0, &desired, &fallbackObtained, allowedChanges);
            if (deviceId_ != 0) {
                SLOG_WARN(
                    "Audio device open failed ({}). Falling back to dummy driver.", fallbackError);
                obtained = fallbackObtained;
                config_.deviceName = "dummy";
            }
        }
    }

    if (deviceId_ == 0) {
        if (sdlInitialized_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            sdlInitialized_ = false;
        }
        return Result<std::monostate, ApiError>::error(
            ApiError(std::string("SDL open audio device failed: ") + SDL_GetError()));
    }

    config_.sampleRate = obtained.freq;
    config_.channels = obtained.channels;
    config_.bufferFrames = obtained.samples;
    deviceFormat_ = obtained.format;
    if (config_.deviceName.empty()) {
        config_.deviceName = "default";
    }

    voice_.setSampleRate(static_cast<double>(config_.sampleRate));

    SDL_PauseAudioDevice(deviceId_, 0);

    SLOG_INFO(
        "Audio engine started: {} Hz, {} ch, {} frames, format=0x{:x}",
        config_.sampleRate,
        config_.channels,
        config_.bufferFrames,
        deviceFormat_);

    return Result<std::monostate, ApiError>::okay(std::monostate{});
}

void AudioEngine::stop()
{
    if (deviceId_ != 0) {
        SDL_CloseAudioDevice(deviceId_);
        deviceId_ = 0;
    }

    if (sdlInitialized_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sdlInitialized_ = false;
    }

    active_.store(false);
    activeNoteId_.store(0);
}

uint32_t AudioEngine::enqueueNoteOn(
    double frequencyHz,
    double amplitude,
    double attackSeconds,
    double releaseSeconds,
    Audio::Waveform waveform,
    uint32_t noteId)
{
    AudioCommand command;
    command.type = CommandType::NoteOn;
    command.noteOn.frequencyHz = frequencyHz;
    command.noteOn.amplitude = amplitude;
    command.noteOn.attackSeconds = attackSeconds;
    command.noteOn.releaseSeconds = releaseSeconds;
    command.noteOn.waveform = waveform;

    if (noteId == 0) {
        noteId = nextNoteId_.fetch_add(1);
    }
    command.noteOn.noteId = noteId;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        commandQueue_.push_back(command);
    }

    return noteId;
}

void AudioEngine::enqueueNoteOff(uint32_t noteId)
{
    AudioCommand command;
    command.type = CommandType::NoteOff;
    command.noteOff.noteId = noteId;

    std::lock_guard<std::mutex> lock(queueMutex_);
    commandQueue_.push_back(command);
}

AudioStatus AudioEngine::getStatus() const
{
    AudioStatus status;
    status.active = active_.load();
    status.noteId = activeNoteId_.load();
    status.frequencyHz = currentFrequencyHz_.load();
    status.amplitude = currentAmplitude_.load();
    status.envelopeLevel = currentEnvelopeLevel_.load();
    status.envelopeState = static_cast<Audio::EnvelopeState>(currentEnvelopeState_.load());
    status.waveform = static_cast<Audio::Waveform>(currentWaveform_.load());
    status.sampleRate = static_cast<double>(config_.sampleRate);
    status.deviceName = config_.deviceName;
    return status;
}

void AudioEngine::audioCallback(void* userdata, Uint8* stream, int len)
{
    if (!userdata || !stream || len <= 0) {
        return;
    }

    auto* engine = static_cast<AudioEngine*>(userdata);
    const int channels = std::max(1, engine->config_.channels);
    const int frameBytes = (SDL_AUDIO_BITSIZE(engine->deviceFormat_) / 8) * channels;
    if (frameBytes <= 0) {
        SDL_memset(stream, 0, len);
        return;
    }
    const int frames = len / frameBytes;
    engine->renderToStream(stream, frames, channels);
}

void AudioEngine::render(float* out, int frames, int channels)
{
    std::vector<AudioCommand> pending;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        pending.swap(commandQueue_);
    }

    for (const auto& command : pending) {
        applyCommand(command);
    }

    for (int i = 0; i < frames; ++i) {
        const double sample = voice_.renderSample();
        const float outSample = static_cast<float>(std::clamp(sample, -1.0, 1.0));

        const int base = i * channels;
        for (int ch = 0; ch < channels; ++ch) {
            out[base + ch] = outSample;
        }
    }

    updateStatus();
}

void AudioEngine::renderToStream(Uint8* stream, int frames, int channels)
{
    const int samples = frames * channels;
    if (samples <= 0) {
        return;
    }

    if (isFloatOutput()) {
        auto* out = reinterpret_cast<float*>(stream);
        render(out, frames, channels);
        return;
    }

    mixBuffer_.assign(static_cast<size_t>(samples), 0.0f);
    render(mixBuffer_.data(), frames, channels);

    if (deviceFormat_ == AUDIO_S16SYS) {
        s16Buffer_.resize(static_cast<size_t>(samples));
        for (int i = 0; i < samples; ++i) {
            const float value = std::clamp(mixBuffer_[static_cast<size_t>(i)], -1.0f, 1.0f);
            s16Buffer_[static_cast<size_t>(i)] =
                static_cast<int16_t>(std::lround(value * 32767.0f));
        }
        SDL_memcpy(stream, s16Buffer_.data(), static_cast<size_t>(samples) * sizeof(int16_t));
        return;
    }

    SDL_memset(stream, 0, static_cast<size_t>(samples) * (SDL_AUDIO_BITSIZE(deviceFormat_) / 8));
}

bool AudioEngine::isFloatOutput() const
{
    return deviceFormat_ == AUDIO_F32SYS;
}

void AudioEngine::applyCommand(const AudioCommand& command)
{
    if (command.type == CommandType::NoteOn) {
        voice_.noteOn(
            command.noteOn.frequencyHz,
            command.noteOn.amplitude,
            command.noteOn.attackSeconds,
            command.noteOn.releaseSeconds,
            command.noteOn.waveform);
        activeNoteId_.store(command.noteOn.noteId);
        return;
    }

    const uint32_t currentId = activeNoteId_.load();
    if (command.noteOff.noteId == 0 || command.noteOff.noteId == currentId) {
        voice_.noteOff();
    }
}

void AudioEngine::updateStatus()
{
    const bool active = voice_.isActive();
    active_.store(active);

    if (!active) {
        activeNoteId_.store(0);
    }

    currentFrequencyHz_.store(voice_.getFrequency());
    currentAmplitude_.store(voice_.getAmplitude());
    currentEnvelopeLevel_.store(voice_.getEnvelopeLevel());
    currentEnvelopeState_.store(static_cast<int>(voice_.getEnvelopeState()));
    currentWaveform_.store(static_cast<int>(voice_.getWaveform()));
}

} // namespace AudioProcess
} // namespace DirtSim
