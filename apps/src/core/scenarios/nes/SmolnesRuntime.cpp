#include "SmolnesRuntime.h"

#include "SmolnesRuntimeBackend.h"

namespace DirtSim {

namespace {

constexpr const char* kCreateRuntimeError = "Failed to allocate smolnes runtime backend instance.";

} // namespace

SmolnesRuntime::SmolnesRuntime() : runtimeHandle_(smolnesRuntimeCreate())
{}

SmolnesRuntime::~SmolnesRuntime()
{
    if (runtimeHandle_ == nullptr) {
        return;
    }

    smolnesRuntimeDestroy(runtimeHandle_);
    runtimeHandle_ = nullptr;
}

bool SmolnesRuntime::start(const std::string& romPath)
{
    if (runtimeHandle_ == nullptr) {
        return false;
    }
    return smolnesRuntimeStart(runtimeHandle_, romPath.c_str());
}

bool SmolnesRuntime::runFrames(uint32_t frameCount, uint32_t timeoutMs)
{
    if (runtimeHandle_ == nullptr) {
        return false;
    }
    return smolnesRuntimeRunFrames(runtimeHandle_, frameCount, timeoutMs);
}

void SmolnesRuntime::stop()
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeStop(runtimeHandle_);
}

void SmolnesRuntime::setController1State(uint8_t buttonMask)
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeSetController1State(runtimeHandle_, buttonMask);
}

bool SmolnesRuntime::isHealthy() const
{
    if (runtimeHandle_ == nullptr) {
        return false;
    }
    return smolnesRuntimeIsHealthy(runtimeHandle_);
}

bool SmolnesRuntime::isRunning() const
{
    if (runtimeHandle_ == nullptr) {
        return false;
    }
    return smolnesRuntimeIsRunning(runtimeHandle_);
}

uint64_t SmolnesRuntime::getRenderedFrameCount() const
{
    if (runtimeHandle_ == nullptr) {
        return 0;
    }
    return smolnesRuntimeGetRenderedFrameCount(runtimeHandle_);
}

bool SmolnesRuntime::copyLatestFrameInto(ScenarioVideoFrame& frame) const
{
    if (runtimeHandle_ == nullptr) {
        return false;
    }

    frame.width = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_WIDTH);
    frame.height = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_HEIGHT);
    if (frame.pixels.size() != SMOLNES_RUNTIME_FRAME_BYTES) {
        frame.pixels.resize(SMOLNES_RUNTIME_FRAME_BYTES);
    }

    return smolnesRuntimeCopyLatestFrame(
        runtimeHandle_,
        reinterpret_cast<uint8_t*>(frame.pixels.data()),
        static_cast<uint32_t>(frame.pixels.size()),
        &frame.frame_id);
}

std::optional<ScenarioVideoFrame> SmolnesRuntime::copyLatestFrame() const
{
    ScenarioVideoFrame frame;
    if (!copyLatestFrameInto(frame)) {
        return std::nullopt;
    }
    return frame;
}

std::optional<SmolnesRuntime::MemorySnapshot> SmolnesRuntime::copyMemorySnapshot() const
{
    if (runtimeHandle_ == nullptr) {
        return std::nullopt;
    }

    MemorySnapshot snapshot{};
    if (!smolnesRuntimeCopyCpuRam(
            runtimeHandle_, snapshot.cpuRam.data(), SMOLNES_RUNTIME_CPU_RAM_BYTES)) {
        return std::nullopt;
    }
    if (!smolnesRuntimeCopyPrgRam(
            runtimeHandle_, snapshot.prgRam.data(), SMOLNES_RUNTIME_PRG_RAM_BYTES)) {
        return std::nullopt;
    }
    return snapshot;
}

std::optional<SmolnesRuntime::ProfilingSnapshot> SmolnesRuntime::copyProfilingSnapshot() const
{
    if (runtimeHandle_ == nullptr) {
        return std::nullopt;
    }

    SmolnesRuntimeProfilingSnapshot raw{};
    if (!smolnesRuntimeCopyProfilingSnapshot(runtimeHandle_, &raw)) {
        return std::nullopt;
    }

    ProfilingSnapshot snapshot{};
    snapshot.runFramesWaitMs = raw.run_frames_wait_ms;
    snapshot.runFramesWaitCalls = raw.run_frames_wait_calls;
    snapshot.runtimeThreadIdleWaitMs = raw.runtime_thread_idle_wait_ms;
    snapshot.runtimeThreadIdleWaitCalls = raw.runtime_thread_idle_wait_calls;
    snapshot.runtimeThreadFrameExecutionMs = raw.runtime_thread_frame_execution_ms;
    snapshot.runtimeThreadFrameExecutionCalls = raw.runtime_thread_frame_execution_calls;
    snapshot.runtimeThreadFrameSubmitMs = raw.runtime_thread_frame_submit_ms;
    snapshot.runtimeThreadFrameSubmitCalls = raw.runtime_thread_frame_submit_calls;
    snapshot.runtimeThreadEventPollMs = raw.runtime_thread_event_poll_ms;
    snapshot.runtimeThreadEventPollCalls = raw.runtime_thread_event_poll_calls;
    snapshot.runtimeThreadPresentMs = raw.runtime_thread_present_ms;
    snapshot.runtimeThreadPresentCalls = raw.runtime_thread_present_calls;
    snapshot.memorySnapshotCopyMs = raw.memory_snapshot_copy_ms;
    snapshot.memorySnapshotCopyCalls = raw.memory_snapshot_copy_calls;
    return snapshot;
}

std::string SmolnesRuntime::getLastError() const
{
    if (runtimeHandle_ == nullptr) {
        return std::string{ kCreateRuntimeError };
    }

    char buffer[256] = { 0 };
    smolnesRuntimeGetLastErrorCopy(runtimeHandle_, buffer, sizeof(buffer));
    return std::string{ buffer };
}

} // namespace DirtSim
