#include "AudioEngine.h"
#include "core/LoggingChannels.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
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

bool isUsbDeviceName(const std::string& name)
{
    std::string lowerName;
    lowerName.reserve(name.size());
    for (const char value : name) {
        lowerName.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
    }
    return lowerName.find("usb") != std::string::npos;
}

std::string joinDeviceNames(const std::vector<std::string>& devices)
{
    std::ostringstream stream;
    for (size_t index = 0; index < devices.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << devices[index];
    }
    return stream.str();
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
    const int maxAttempts = 1;
    const char* currentDriver = SDL_GetCurrentAudioDriver();
    if (currentDriver) {
        SLOG_INFO("Audio driver: {}", currentDriver);
    }
    else {
        SLOG_WARN("Audio driver not available");
    }

    std::string openError;
    std::string openedDeviceName;
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
            else {
                openedDeviceName = name ? name : "default";
            }
        }
        return deviceId;
    };

    if (!config_.deviceName.empty()) {
        deviceId_ = openWithRetries(deviceName, &obtained, maxAttempts);
    }
    if (deviceId_ == 0 && config_.deviceName.empty()) {
        const auto devices = listOutputDevices();
        if (devices.empty()) {
            SLOG_WARN("No SDL audio output devices reported");
        }
        else {
            auto orderedDevices = devices;
            std::stable_partition(
                orderedDevices.begin(), orderedDevices.end(), [](const std::string& name) {
                    return isUsbDeviceName(name);
                });
            SLOG_INFO("Audio device probe order: {}", joinDeviceNames(orderedDevices));
            for (const auto& name : orderedDevices) {
                SDL_AudioSpec probe{};
                const SDL_AudioDeviceID probeId =
                    openWithRetries(name.c_str(), &probe, maxAttempts);
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
                openedDeviceName = "dummy";
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

    if (obtained.format != AUDIO_F32SYS && obtained.format != AUDIO_S16SYS) {
        SDL_CloseAudioDevice(deviceId_);
        deviceId_ = 0;
        if (sdlInitialized_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            sdlInitialized_ = false;
        }
        return Result<std::monostate, ApiError>::error(
            ApiError("Unsupported SDL audio format: " + std::to_string(obtained.format)));
    }

    config_.sampleRate = obtained.freq;
    config_.channels = obtained.channels;
    config_.bufferFrames = obtained.samples;
    deviceFormat_ = obtained.format;
    if (config_.deviceName.empty()) {
        config_.deviceName = "default";
    }
    if (openedDeviceName.empty()) {
        openedDeviceName = config_.deviceName;
    }

    const int maxChannels = std::max(1, config_.channels);
    const size_t maxSamples =
        static_cast<size_t>(std::max(1, config_.bufferFrames)) * static_cast<size_t>(maxChannels);
    mixBuffer_.assign(maxSamples, 0.0f);
    s16Buffer_.assign(maxSamples, 0);

    clearVoiceRuntimeState();
    for (size_t voiceIndex = 0; voiceIndex < voices_.size(); ++voiceIndex) {
        voices_[voiceIndex].voice.setSampleRate(static_cast<double>(config_.sampleRate));
        voices_[voiceIndex].voiceIndex = voiceIndex;
    }
    nextVoiceStartOrder_ = 1;
    commandReadIndex_.store(0, std::memory_order_relaxed);
    commandWriteIndex_.store(0, std::memory_order_relaxed);

    SDL_PauseAudioDevice(deviceId_, 0);

    SLOG_INFO("Audio device opened: {}", openedDeviceName);
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

    clearVoiceRuntimeState();
    commandReadIndex_.store(0, std::memory_order_relaxed);
    commandWriteIndex_.store(0, std::memory_order_relaxed);
    nextVoiceStartOrder_ = 1;
}

uint32_t AudioEngine::enqueueNoteOn(
    double frequencyHz,
    double amplitude,
    double attackSeconds,
    double durationSeconds,
    double releaseSeconds,
    Audio::Waveform waveform,
    uint32_t noteId)
{
    AudioCommand command;
    command.type = CommandType::NoteOn;
    command.noteOn.frequencyHz = frequencyHz;
    command.noteOn.amplitude = amplitude;
    command.noteOn.attackSeconds = attackSeconds;
    command.noteOn.durationSeconds = durationSeconds;
    command.noteOn.releaseSeconds = releaseSeconds;
    command.noteOn.waveform = waveform;

    if (noteId == 0) {
        noteId = nextNoteId_.fetch_add(1);
    }
    command.noteOn.noteId = noteId;

    enqueueCommand(command);

    return noteId;
}

void AudioEngine::enqueueNoteOff(uint32_t noteId)
{
    AudioCommand command;
    command.type = CommandType::NoteOff;
    command.noteOff.noteId = noteId;

    enqueueCommand(command);
}

void AudioEngine::setMasterVolumePercent(int volumePercent)
{
    masterVolumePercent_.store(std::clamp(volumePercent, 0, 100), std::memory_order_relaxed);
}

int AudioEngine::getMasterVolumePercent() const
{
    return masterVolumePercent_.load(std::memory_order_relaxed);
}

AudioStatus AudioEngine::getStatus() const
{
    if (deviceId_ == 0) {
        return buildStatusSnapshotUnlocked();
    }

    SDL_LockAudioDevice(deviceId_);
    AudioStatus status = buildStatusSnapshotUnlocked();
    SDL_UnlockAudioDevice(deviceId_);
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
    engine->renderToStream(stream, frames, channels, len);
}

void AudioEngine::render(float* out, int frames, int channels)
{
    drainCommands();
    const float masterGain =
        static_cast<float>(masterVolumePercent_.load(std::memory_order_relaxed)) / 100.0f;

    for (int i = 0; i < frames; ++i) {
        double mixedSample = 0.0;
        for (auto& voice : voices_) {
            mixedSample += voice.voice.renderSample();
        }
        const float outSample = static_cast<float>(std::clamp(mixedSample, -1.0, 1.0)) * masterGain;

        const int base = i * channels;
        for (int ch = 0; ch < channels; ++ch) {
            out[base + ch] = outSample;
        }

        for (auto& voice : voices_) {
            if (voice.noteId == 0) {
                continue;
            }

            if (voice.autoNoteOffFramesRemaining > 0) {
                --voice.autoNoteOffFramesRemaining;
                if (voice.autoNoteOffFramesRemaining == 0) {
                    voice.voice.noteOff();
                    voice.autoNoteOffFramesRemaining = -1;
                    voice.holdState = AudioNoteHoldState::Releasing;
                }
            }

            if (!voice.voice.isActive()) {
                voice.noteId = 0;
                voice.autoNoteOffFramesRemaining = -1;
                voice.startOrder = 0;
                voice.holdState = AudioNoteHoldState::Held;
            }
        }
    }
}

void AudioEngine::renderToStream(Uint8* stream, int frames, int channels, int len)
{
    const int samples = frames * channels;
    if (samples <= 0) {
        if (len > 0) {
            SDL_memset(stream, 0, static_cast<size_t>(len));
        }
        return;
    }

    if (isFloatOutput()) {
        auto* out = reinterpret_cast<float*>(stream);
        render(out, frames, channels);
        const int renderedBytes = samples * static_cast<int>(sizeof(float));
        if (len > renderedBytes) {
            SDL_memset(stream + renderedBytes, 0, static_cast<size_t>(len - renderedBytes));
        }
        return;
    }

    if (static_cast<size_t>(samples) > mixBuffer_.size()) {
        SDL_memset(stream, 0, static_cast<size_t>(len));
        return;
    }

    render(mixBuffer_.data(), frames, channels);

    if (deviceFormat_ == AUDIO_S16SYS) {
        if (static_cast<size_t>(samples) > s16Buffer_.size()) {
            SDL_memset(stream, 0, static_cast<size_t>(len));
            return;
        }
        for (int i = 0; i < samples; ++i) {
            const float value = std::clamp(mixBuffer_[static_cast<size_t>(i)], -1.0f, 1.0f);
            s16Buffer_[static_cast<size_t>(i)] =
                static_cast<int16_t>(std::lround(value * 32767.0f));
        }
        SDL_memcpy(stream, s16Buffer_.data(), static_cast<size_t>(samples) * sizeof(int16_t));
        const int renderedBytes = samples * static_cast<int>(sizeof(int16_t));
        if (len > renderedBytes) {
            SDL_memset(stream + renderedBytes, 0, static_cast<size_t>(len - renderedBytes));
        }
        return;
    }

    SDL_memset(stream, 0, static_cast<size_t>(len));
}

bool AudioEngine::isFloatOutput() const
{
    return deviceFormat_ == AUDIO_F32SYS;
}

bool AudioEngine::enqueueCommand(const AudioCommand& command)
{
    const size_t writeIndex = commandWriteIndex_.load(std::memory_order_relaxed);
    const size_t readIndex = commandReadIndex_.load(std::memory_order_acquire);
    if (writeIndex - readIndex >= kCommandQueueCapacity) {
        SLOG_WARN("Audio command queue full; dropping command");
        return false;
    }

    commandQueue_[writeIndex % kCommandQueueCapacity] = command;
    commandWriteIndex_.store(writeIndex + 1, std::memory_order_release);
    return true;
}

void AudioEngine::drainCommands()
{
    size_t readIndex = commandReadIndex_.load(std::memory_order_relaxed);
    const size_t writeIndex = commandWriteIndex_.load(std::memory_order_acquire);
    while (readIndex < writeIndex) {
        applyCommand(commandQueue_[readIndex % kCommandQueueCapacity]);
        ++readIndex;
    }
    commandReadIndex_.store(readIndex, std::memory_order_release);
}

void AudioEngine::applyCommand(const AudioCommand& command)
{
    if (command.type == CommandType::NoteOn) {
        const size_t voiceIndex = selectVoiceIndexForNoteOn(command.noteOn.noteId);
        startVoice(voiceIndex, command.noteOn);
        return;
    }

    if (command.noteOff.noteId == 0) {
        for (size_t voiceIndex = 0; voiceIndex < voices_.size(); ++voiceIndex) {
            releaseVoice(voiceIndex);
        }
        return;
    }

    for (size_t voiceIndex = 0; voiceIndex < voices_.size(); ++voiceIndex) {
        if (voices_[voiceIndex].noteId == command.noteOff.noteId) {
            releaseVoice(voiceIndex);
            return;
        }
    }
}

size_t AudioEngine::selectVoiceIndexToSteal() const
{
    struct Candidate {
        bool releasing = false;
        double envelopeLevel = std::numeric_limits<double>::infinity();
        uint64_t startOrder = 0;
        size_t voiceIndex = 0;
    };

    bool hasCandidate = false;
    Candidate best{};
    for (size_t voiceIndex = 0; voiceIndex < voices_.size(); ++voiceIndex) {
        const auto& voice = voices_[voiceIndex];
        if (voice.noteId == 0 || !voice.voice.isActive()) {
            continue;
        }

        Candidate current{
            .releasing = voice.holdState == AudioNoteHoldState::Releasing,
            .envelopeLevel = voice.voice.getEnvelopeLevel(),
            .startOrder = voice.startOrder,
            .voiceIndex = voiceIndex,
        };

        if (!hasCandidate) {
            best = current;
            hasCandidate = true;
            continue;
        }

        if (current.releasing != best.releasing) {
            if (current.releasing) {
                best = current;
            }
            continue;
        }

        if (current.envelopeLevel < best.envelopeLevel) {
            best = current;
            continue;
        }

        if (current.envelopeLevel > best.envelopeLevel) {
            continue;
        }

        if (current.startOrder < best.startOrder) {
            best = current;
            continue;
        }

        if (current.startOrder > best.startOrder) {
            continue;
        }

        if (current.voiceIndex < best.voiceIndex) {
            best = current;
        }
    }

    if (!hasCandidate) {
        return 0;
    }
    return best.voiceIndex;
}

size_t AudioEngine::selectVoiceIndexForNoteOn(uint32_t noteId) const
{
    for (size_t voiceIndex = 0; voiceIndex < voices_.size(); ++voiceIndex) {
        const auto& voice = voices_[voiceIndex];
        if (voice.noteId == noteId && voice.voice.isActive()) {
            return voiceIndex;
        }
    }

    for (size_t voiceIndex = 0; voiceIndex < voices_.size(); ++voiceIndex) {
        const auto& voice = voices_[voiceIndex];
        if (voice.noteId == 0 || !voice.voice.isActive()) {
            return voiceIndex;
        }
    }

    return selectVoiceIndexToSteal();
}

void AudioEngine::startVoice(size_t voiceIndex, const NoteOnCommand& noteOn)
{
    auto& voice = voices_[voiceIndex];
    voice.voice.noteOn(
        noteOn.frequencyHz,
        noteOn.amplitude,
        noteOn.attackSeconds,
        noteOn.releaseSeconds,
        noteOn.waveform);

    voice.noteId = noteOn.noteId;
    voice.startOrder = nextVoiceStartOrder_++;
    voice.holdState = AudioNoteHoldState::Held;

    if (noteOn.durationSeconds > 0.0) {
        const double frames = noteOn.durationSeconds * static_cast<double>(config_.sampleRate);
        voice.autoNoteOffFramesRemaining =
            std::max<int64_t>(1, static_cast<int64_t>(std::llround(frames)));
    }
    else {
        voice.autoNoteOffFramesRemaining = -1;
    }
}

void AudioEngine::releaseVoice(size_t voiceIndex)
{
    auto& voice = voices_[voiceIndex];
    if (voice.noteId == 0 || !voice.voice.isActive()) {
        return;
    }

    voice.voice.noteOff();
    voice.autoNoteOffFramesRemaining = -1;
    voice.holdState = AudioNoteHoldState::Releasing;
}

AudioStatus AudioEngine::buildStatusSnapshotUnlocked() const
{
    AudioStatus status;
    status.activeNotes.reserve(voices_.size());
    status.sampleRate = static_cast<double>(config_.sampleRate);
    status.deviceName = config_.deviceName;

    for (const auto& voice : voices_) {
        if (voice.noteId == 0 || !voice.voice.isActive()) {
            continue;
        }

        status.activeNotes.push_back(
            ActiveAudioNoteStatus{
                .noteId = voice.noteId,
                .frequencyHz = voice.voice.getFrequency(),
                .amplitude = voice.voice.getAmplitude(),
                .waveform = voice.voice.getWaveform(),
                .envelopeState = voice.voice.getEnvelopeState(),
                .holdState = voice.holdState,
            });
    }

    return status;
}

void AudioEngine::clearVoiceRuntimeState()
{
    for (auto& voice : voices_) {
        voice.noteId = 0;
        voice.autoNoteOffFramesRemaining = -1;
        voice.startOrder = 0;
        voice.holdState = AudioNoteHoldState::Held;
    }
}

} // namespace AudioProcess
} // namespace DirtSim
