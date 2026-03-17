#pragma once

#include "core/RenderMessage.h"
#include "core/scenarios/nes/NesPaletteFrame.h"
#include "core/scenarios/nes/SmolnesRuntimeBackend.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace DirtSim {

enum class SmolnesRuntimePacingMode : uint8_t {
    Lockstep = 0,
    Realtime = 1,
};

class SmolnesRuntime {
public:
    struct MemorySnapshot {
        std::array<uint8_t, SMOLNES_RUNTIME_CPU_RAM_BYTES> cpuRam{};
        std::array<uint8_t, SMOLNES_RUNTIME_PRG_RAM_BYTES> prgRam{};
    };

    struct ProfilingSnapshot {
        double runFramesWaitMs = 0.0;
        uint64_t runFramesWaitCalls = 0;
        double runtimeThreadIdleWaitMs = 0.0;
        uint64_t runtimeThreadIdleWaitCalls = 0;
        double runtimeThreadCpuStepMs = 0.0;
        uint64_t runtimeThreadCpuStepCalls = 0;
        double runtimeThreadFrameExecutionMs = 0.0;
        uint64_t runtimeThreadFrameExecutionCalls = 0;
        double runtimeThreadPpuStepMs = 0.0;
        uint64_t runtimeThreadPpuStepCalls = 0;
        double runtimeThreadPpuVisiblePixelsMs = 0.0;
        uint64_t runtimeThreadPpuVisiblePixelsCalls = 0;
        double runtimeThreadPpuSpriteEvalMs = 0.0;
        uint64_t runtimeThreadPpuSpriteEvalCalls = 0;
        double runtimeThreadPpuPrefetchMs = 0.0;
        uint64_t runtimeThreadPpuPrefetchCalls = 0;
        double runtimeThreadPpuOtherMs = 0.0;
        uint64_t runtimeThreadPpuOtherCalls = 0;
        double runtimeThreadFrameSubmitMs = 0.0;
        uint64_t runtimeThreadFrameSubmitCalls = 0;
        double runtimeThreadEventPollMs = 0.0;
        uint64_t runtimeThreadEventPollCalls = 0;
        double runtimeThreadPresentMs = 0.0;
        uint64_t runtimeThreadPresentCalls = 0;
        double memorySnapshotCopyMs = 0.0;
        uint64_t memorySnapshotCopyCalls = 0;
    };

    struct ControllerSnapshot {
        uint64_t latestFrameId = 0;
        uint64_t controller1AppliedFrameId = 0;
        uint64_t controller1ObservedTimestampNs = 0;
        uint64_t controller1LatchTimestampNs = 0;
        uint64_t controller1RequestTimestampNs = 0;
        uint64_t controller1SequenceId = 0;
        uint8_t controller1State = 0;
    };

    SmolnesRuntime();
    virtual ~SmolnesRuntime();

    SmolnesRuntime(const SmolnesRuntime&) = delete;
    SmolnesRuntime& operator=(const SmolnesRuntime&) = delete;

    virtual bool start(const std::string& romPath);
    virtual bool runFrames(uint32_t frameCount, uint32_t timeoutMs);
    virtual void stop();
    virtual void setController1State(uint8_t buttonMask);
    virtual void setController1StateObserved(uint8_t buttonMask, uint64_t observedTimestampNs);

    virtual bool isHealthy() const;
    virtual bool isRunning() const;
    virtual uint64_t getRenderedFrameCount() const;
    virtual bool copyLatestFrameInto(ScenarioVideoFrame& frame) const;
    virtual std::optional<ScenarioVideoFrame> copyLatestFrame() const;
    virtual std::optional<NesPaletteFrame> copyLatestPaletteFrame() const;
    virtual std::optional<ControllerSnapshot> copyControllerSnapshot() const;
    struct ApuSnapshot {
        bool pulse1Enabled = false;
        bool pulse2Enabled = false;
        bool triangleEnabled = false;
        bool noiseEnabled = false;
        uint8_t pulse1LengthCounter = 0;
        uint8_t pulse2LengthCounter = 0;
        uint8_t triangleLengthCounter = 0;
        uint8_t noiseLengthCounter = 0;
        uint16_t pulse1TimerPeriod = 0;
        uint16_t pulse2TimerPeriod = 0;
        uint16_t triangleTimerPeriod = 0;
        uint16_t noiseTimerPeriod = 0;
        uint8_t pulse1Duty = 0;
        uint8_t pulse2Duty = 0;
        bool noiseMode = false;
        bool frameCounterMode5Step = false;
        uint64_t registerWriteCount = 0;
        uint64_t totalSamplesGenerated = 0;

        // Audio playback stats (from NesAudioPlayer, not the APU itself).
        uint64_t audioUnderruns = 0;
        uint64_t audioOverruns = 0;
        uint64_t audioCallbackCalls = 0;
        uint64_t audioSamplesDropped = 0;
    };

    virtual std::optional<MemorySnapshot> copyMemorySnapshot() const;
    virtual std::optional<ProfilingSnapshot> copyProfilingSnapshot() const;
    virtual std::optional<ApuSnapshot> copyApuSnapshot() const;
    virtual uint32_t copyApuSamples(float* buffer, uint32_t maxSamples) const;
    virtual void setApuSampleCallback(SmolnesApuSampleCallback callback, void* userdata);
    virtual void setPacingMode(SmolnesRuntimePacingMode mode);
    virtual std::string getLastError() const;

private:
    SmolnesRuntimeHandle* runtimeHandle_ = nullptr;
};

} // namespace DirtSim
