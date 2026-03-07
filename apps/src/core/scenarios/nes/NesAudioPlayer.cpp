#include "core/scenarios/nes/NesAudioPlayer.h"

#include "core/LoggingChannels.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace DirtSim {

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
    std::string lower;
    lower.reserve(name.size());
    for (const char c : name) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower.find("usb") != std::string::npos;
}

} // namespace

NesAudioPlayer::NesAudioPlayer() = default;

NesAudioPlayer::~NesAudioPlayer()
{
    stop();
}

bool NesAudioPlayer::start(int sampleRate)
{
    stop();
    sampleRate_ = sampleRate;

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            LOG_ERROR(Scenario, "NesAudioPlayer: SDL audio init failed: {}", SDL_GetError());
            return false;
        }
        sdlAudioInitialized_ = true;
    }

    SDL_AudioSpec desired{};
    desired.freq = sampleRate_;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 512;
    desired.callback = &NesAudioPlayer::audioCallback;
    desired.userdata = this;

    const int allowedChanges = SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE;

    // Try USB devices first, then fallback to default.
    SDL_AudioSpec obtained{};
    auto devices = listOutputDevices();
    std::stable_partition(devices.begin(), devices.end(), [](const std::string& name) {
        return isUsbDeviceName(name);
    });

    for (const auto& name : devices) {
        SDL_AudioSpec probe{};
        const SDL_AudioDeviceID id =
            SDL_OpenAudioDevice(name.c_str(), 0, &desired, &probe, allowedChanges);
        if (id != 0) {
            deviceId_ = id;
            obtained = probe;
            LOG_INFO(Scenario, "NesAudioPlayer: Opened device '{}'", name);
            break;
        }
    }

    // Fallback to default device.
    if (deviceId_ == 0) {
        deviceId_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, allowedChanges);
    }

    if (deviceId_ == 0) {
        LOG_ERROR(Scenario, "NesAudioPlayer: Failed to open audio device: {}", SDL_GetError());
        if (sdlAudioInitialized_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            sdlAudioInitialized_ = false;
        }
        return false;
    }

    if (obtained.format != AUDIO_S16SYS && obtained.format != AUDIO_F32SYS) {
        LOG_ERROR(Scenario, "NesAudioPlayer: Unsupported audio format: 0x{:x}", obtained.format);
        SDL_CloseAudioDevice(deviceId_);
        deviceId_ = 0;
        if (sdlAudioInitialized_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            sdlAudioInitialized_ = false;
        }
        return false;
    }

    sampleRate_ = obtained.freq;
    deviceChannels_ = obtained.channels;
    deviceFormat_ = obtained.format;

    const size_t maxSamples =
        static_cast<size_t>(obtained.samples) * static_cast<size_t>(deviceChannels_);
    s16Buffer_.resize(maxSamples, 0);

    readPos_.store(0, std::memory_order_relaxed);
    writePos_.store(0, std::memory_order_relaxed);
    underrunCount_.store(0, std::memory_order_relaxed);
    overrunCount_.store(0, std::memory_order_relaxed);
    callbackCount_.store(0, std::memory_order_relaxed);
    samplesDroppedCount_.store(0, std::memory_order_relaxed);

    SDL_PauseAudioDevice(deviceId_, 0);

    LOG_INFO(
        Scenario,
        "NesAudioPlayer: Started {} Hz, {} ch, format=0x{:x}",
        sampleRate_,
        deviceChannels_,
        deviceFormat_);

    return true;
}

void NesAudioPlayer::stop()
{
    if (deviceId_ != 0) {
        SDL_CloseAudioDevice(deviceId_);
        deviceId_ = 0;
    }

    if (sdlAudioInitialized_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sdlAudioInitialized_ = false;
    }

    readPos_.store(0, std::memory_order_relaxed);
    writePos_.store(0, std::memory_order_relaxed);
}

bool NesAudioPlayer::isRunning() const
{
    return deviceId_ != 0;
}

void NesAudioPlayer::setVolumePercent(int percent)
{
    volumePercent_.store(std::clamp(percent, 0, 100), std::memory_order_relaxed);
}

NesAudioPlayer::Stats NesAudioPlayer::getStats() const
{
    Stats s;
    s.underruns = underrunCount_.load(std::memory_order_relaxed);
    s.overruns = overrunCount_.load(std::memory_order_relaxed);
    s.callbackCalls = callbackCount_.load(std::memory_order_relaxed);
    s.samplesDropped = samplesDroppedCount_.load(std::memory_order_relaxed);
    return s;
}

void NesAudioPlayer::pushSamples(const float* samples, uint32_t count)
{
    uint32_t wp = writePos_.load(std::memory_order_relaxed);
    const uint32_t rp = readPos_.load(std::memory_order_acquire);
    const uint32_t available = kRingCapacity - (wp - rp);

    if (count <= available) {
        for (uint32_t i = 0; i < count; ++i) {
            ring_[wp % kRingCapacity] = samples[i];
            wp++;
        }
    }
    else {
        // Write what fits, drop the rest.
        overrunCount_.fetch_add(1, std::memory_order_relaxed);
        samplesDroppedCount_.fetch_add(count - available, std::memory_order_relaxed);
        for (uint32_t i = 0; i < available; ++i) {
            ring_[wp % kRingCapacity] = samples[i];
            wp++;
        }
    }

    writePos_.store(wp, std::memory_order_release);
}

void NesAudioPlayer::audioCallback(void* userdata, Uint8* stream, int len)
{
    if (!userdata || !stream || len <= 0) {
        return;
    }

    auto* player = static_cast<NesAudioPlayer*>(userdata);
    player->renderToStream(stream, len);
}

void NesAudioPlayer::renderToStream(Uint8* stream, int len)
{
    const int bytesPerSample = (SDL_AUDIO_BITSIZE(deviceFormat_) / 8) * deviceChannels_;
    if (bytesPerSample <= 0) {
        SDL_memset(stream, 0, static_cast<size_t>(len));
        return;
    }
    const int frames = len / bytesPerSample;

    uint32_t rp = readPos_.load(std::memory_order_relaxed);
    const uint32_t wp = writePos_.load(std::memory_order_acquire);
    const uint32_t available = wp - rp;

    callbackCount_.fetch_add(1, std::memory_order_relaxed);
    const int samplesToRead = std::min(static_cast<int>(available), frames);
    if (samplesToRead < frames) {
        underrunCount_.fetch_add(1, std::memory_order_relaxed);
    }
    const float gain = static_cast<float>(volumePercent_.load(std::memory_order_relaxed)) / 100.0f;

    if (deviceFormat_ == AUDIO_F32SYS) {
        auto* out = reinterpret_cast<float*>(stream);
        for (int i = 0; i < samplesToRead; ++i) {
            const float scaled = std::clamp(ring_[rp % kRingCapacity] * gain, -1.0f, 1.0f);
            rp++;
            for (int ch = 0; ch < deviceChannels_; ++ch) {
                out[i * deviceChannels_ + ch] = scaled;
            }
        }
        // Fill remainder with silence.
        for (int i = samplesToRead; i < frames; ++i) {
            for (int ch = 0; ch < deviceChannels_; ++ch) {
                out[i * deviceChannels_ + ch] = 0.0f;
            }
        }
    }
    else {
        // S16SYS.
        auto* out = reinterpret_cast<int16_t*>(stream);
        for (int i = 0; i < samplesToRead; ++i) {
            const float scaled = std::clamp(ring_[rp % kRingCapacity] * gain, -1.0f, 1.0f);
            rp++;
            const int16_t sample = static_cast<int16_t>(std::lround(scaled * 32767.0f));
            for (int ch = 0; ch < deviceChannels_; ++ch) {
                out[i * deviceChannels_ + ch] = sample;
            }
        }
        // Fill remainder with silence.
        const int totalRemainingSamples = (frames - samplesToRead) * deviceChannels_;
        if (totalRemainingSamples > 0) {
            const int offset = samplesToRead * deviceChannels_;
            SDL_memset(
                &out[offset], 0, static_cast<size_t>(totalRemainingSamples) * sizeof(int16_t));
        }
    }

    readPos_.store(rp, std::memory_order_release);
}

} // namespace DirtSim
