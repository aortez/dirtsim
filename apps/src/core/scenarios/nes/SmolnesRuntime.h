#pragma once

#include "core/RenderMessage.h"
#include "core/scenarios/nes/SmolnesRuntimeBackend.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace DirtSim {

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
        double runtimeThreadFrameExecutionMs = 0.0;
        uint64_t runtimeThreadFrameExecutionCalls = 0;
        double runtimeThreadFrameSubmitMs = 0.0;
        uint64_t runtimeThreadFrameSubmitCalls = 0;
        double runtimeThreadEventPollMs = 0.0;
        uint64_t runtimeThreadEventPollCalls = 0;
        double runtimeThreadPresentMs = 0.0;
        uint64_t runtimeThreadPresentCalls = 0;
        double memorySnapshotCopyMs = 0.0;
        uint64_t memorySnapshotCopyCalls = 0;
    };

    SmolnesRuntime();
    ~SmolnesRuntime();

    SmolnesRuntime(const SmolnesRuntime&) = delete;
    SmolnesRuntime& operator=(const SmolnesRuntime&) = delete;

    bool start(const std::string& romPath);
    bool runFrames(uint32_t frameCount, uint32_t timeoutMs);
    void stop();
    void setController1State(uint8_t buttonMask);

    bool isHealthy() const;
    bool isRunning() const;
    uint64_t getRenderedFrameCount() const;
    bool copyLatestFrameInto(ScenarioVideoFrame& frame) const;
    std::optional<ScenarioVideoFrame> copyLatestFrame() const;
    std::optional<MemorySnapshot> copyMemorySnapshot() const;
    std::optional<ProfilingSnapshot> copyProfilingSnapshot() const;
    std::string getLastError() const;

private:
    SmolnesRuntimeHandle* runtimeHandle_ = nullptr;
};

} // namespace DirtSim
