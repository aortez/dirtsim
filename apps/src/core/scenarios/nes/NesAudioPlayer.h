#pragma once

#include <SDL2/SDL.h>
#include <atomic>
#include <cstdint>

namespace DirtSim {

/**
 * Real-time SDL2 audio output for NES APU samples.
 *
 * Uses a lock-free SPSC ring buffer: game thread pushes samples via
 * pushSamples(), SDL audio callback pulls and plays them.
 */
class NesAudioPlayer {
public:
    NesAudioPlayer();
    ~NesAudioPlayer();

    NesAudioPlayer(const NesAudioPlayer&) = delete;
    NesAudioPlayer& operator=(const NesAudioPlayer&) = delete;

    bool start(int sampleRate = 48000);
    void stop();
    bool isRunning() const;

    void pushSample(float sample);
    void pushSamples(const float* samples, uint32_t count);
    void setVolumePercent(int percent);

    struct Stats {
        uint64_t underruns = 0;      // Callback needed more samples than available.
        uint64_t overruns = 0;       // Push dropped samples because ring was full.
        uint64_t callbackCalls = 0;  // Total audio callback invocations.
        uint64_t samplesDropped = 0; // Total samples dropped due to overruns.
    };
    Stats getStats() const;

private:
    static void audioCallback(void* userdata, Uint8* stream, int len);
    void renderToStream(Uint8* stream, int len);

    static constexpr uint32_t kRingCapacity = 8192;

    float ring_[kRingCapacity] = {};
    std::atomic<uint32_t> readPos_{ 0 };
    std::atomic<uint32_t> writePos_{ 0 };

    SDL_AudioDeviceID deviceId_ = 0;
    SDL_AudioFormat deviceFormat_ = AUDIO_S16SYS;
    bool sdlAudioInitialized_ = false;
    std::atomic<int> volumePercent_{ 100 };
    int sampleRate_ = 48000;
    int deviceChannels_ = 1;

    std::atomic<uint64_t> underrunCount_{ 0 };
    std::atomic<uint64_t> overrunCount_{ 0 };
    std::atomic<uint64_t> callbackCount_{ 0 };
    std::atomic<uint64_t> samplesDroppedCount_{ 0 };
};

} // namespace DirtSim
