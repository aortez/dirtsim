#pragma once

#include "core/Result.h"
#include "core/audio/SynthVoice.h"
#include "server/api/ApiError.h"
#include <SDL2/SDL.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace DirtSim {
namespace AudioProcess {

struct AudioEngineConfig {
    std::string deviceName;
    int sampleRate = 48000;
    int bufferFrames = 512;
    int channels = 2;
};

enum class AudioNoteHoldState : uint8_t {
    Held = 0,
    Releasing = 1,
};

struct ActiveAudioNoteStatus {
    uint32_t noteId = 0;
    double frequencyHz = 0.0;
    double amplitude = 0.0;
    Audio::Waveform waveform = Audio::Waveform::Sine;
    Audio::EnvelopeState envelopeState = Audio::EnvelopeState::Idle;
    AudioNoteHoldState holdState = AudioNoteHoldState::Held;
};

struct AudioStatus {
    std::vector<ActiveAudioNoteStatus> activeNotes;
    double sampleRate = 0.0;
    std::string deviceName;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    Result<std::monostate, ApiError> start(const AudioEngineConfig& config);
    void stop();

    uint32_t enqueueNoteOn(
        double frequencyHz,
        double amplitude,
        double attackSeconds,
        double durationSeconds,
        double releaseSeconds,
        Audio::Waveform waveform,
        uint32_t noteId);
    void enqueueNoteOff(uint32_t noteId);
    void setMasterVolumePercent(int volumePercent);
    int getMasterVolumePercent() const;

    AudioStatus getStatus() const;

private:
    static constexpr size_t kCommandQueueCapacity = 128;
    static constexpr size_t kVoiceCount = 16;

    struct NoteOnCommand {
        double frequencyHz = 440.0;
        double amplitude = 0.5;
        double attackSeconds = 0.01;
        double durationSeconds = 0.12;
        double releaseSeconds = 0.12;
        Audio::Waveform waveform = Audio::Waveform::Sine;
        uint32_t noteId = 0;
    };

    struct NoteOffCommand {
        uint32_t noteId = 0;
    };

    enum class CommandType { NoteOn, NoteOff };

    struct AudioCommand {
        CommandType type = CommandType::NoteOn;
        NoteOnCommand noteOn;
        NoteOffCommand noteOff;
    };

    static void audioCallback(void* userdata, Uint8* stream, int len);
    void render(float* out, int frames, int channels);
    bool enqueueCommand(const AudioCommand& command);
    void drainCommands();
    void applyCommand(const AudioCommand& command);
    size_t selectVoiceIndexToSteal() const;
    size_t selectVoiceIndexForNoteOn(uint32_t noteId) const;
    void startVoice(size_t voiceIndex, const NoteOnCommand& noteOn);
    void releaseVoice(size_t voiceIndex);
    void renderToStream(Uint8* stream, int frames, int channels, int len);
    bool isFloatOutput() const;

    AudioStatus buildStatusSnapshotUnlocked() const;
    void clearVoiceRuntimeState();

    struct VoiceSlot {
        Audio::SynthVoice voice{};
        uint32_t noteId = 0;
        int64_t autoNoteOffFramesRemaining = -1;
        uint64_t startOrder = 0;
        AudioNoteHoldState holdState = AudioNoteHoldState::Held;
        size_t voiceIndex = 0;
    };

    AudioEngineConfig config_{};
    SDL_AudioDeviceID deviceId_ = 0;
    SDL_AudioFormat deviceFormat_ = AUDIO_F32SYS;
    bool sdlInitialized_ = false;

    std::array<VoiceSlot, kVoiceCount> voices_{};
    uint64_t nextVoiceStartOrder_ = 1;
    std::atomic<uint32_t> nextNoteId_{ 1 };

    // Single-producer/single-consumer ring buffer for audio commands.
    std::array<AudioCommand, kCommandQueueCapacity> commandQueue_{};
    std::atomic<size_t> commandReadIndex_{ 0 };
    std::atomic<size_t> commandWriteIndex_{ 0 };
    std::atomic<int> masterVolumePercent_{ 100 };

    std::vector<float> mixBuffer_;
    std::vector<int16_t> s16Buffer_;
};

} // namespace AudioProcess
} // namespace DirtSim
