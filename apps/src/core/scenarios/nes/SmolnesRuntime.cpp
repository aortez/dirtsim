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

void SmolnesRuntime::setController1StateObserved(uint8_t buttonMask, uint64_t observedTimestampNs)
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeSetController1StateObserved(runtimeHandle_, buttonMask, observedTimestampNs);
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

std::optional<NesPaletteFrame> SmolnesRuntime::copyLatestPaletteFrame() const
{
    if (runtimeHandle_ == nullptr) {
        return std::nullopt;
    }

    NesPaletteFrame frame;
    frame.width = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_WIDTH);
    frame.height = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_HEIGHT);
    if (frame.indices.size() != SMOLNES_RUNTIME_PALETTE_FRAME_BYTES) {
        frame.indices.resize(SMOLNES_RUNTIME_PALETTE_FRAME_BYTES);
    }

    if (!smolnesRuntimeCopyLatestPaletteIndices(
            runtimeHandle_,
            frame.indices.data(),
            static_cast<uint32_t>(frame.indices.size()),
            &frame.frameId)) {
        return std::nullopt;
    }

    return frame;
}

std::optional<SmolnesRuntime::LiveSnapshot> SmolnesRuntime::copyLiveSnapshot() const
{
    if (runtimeHandle_ == nullptr) {
        return std::nullopt;
    }

    LiveSnapshot snapshot;
    snapshot.videoFrame.width = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_WIDTH);
    snapshot.videoFrame.height = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_HEIGHT);
    if (snapshot.videoFrame.pixels.size() != SMOLNES_RUNTIME_FRAME_BYTES) {
        snapshot.videoFrame.pixels.resize(SMOLNES_RUNTIME_FRAME_BYTES);
    }

    snapshot.paletteFrame.width = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_WIDTH);
    snapshot.paletteFrame.height = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_HEIGHT);
    if (snapshot.paletteFrame.indices.size() != SMOLNES_RUNTIME_PALETTE_FRAME_BYTES) {
        snapshot.paletteFrame.indices.resize(SMOLNES_RUNTIME_PALETTE_FRAME_BYTES);
    }

    SmolnesRuntimeControllerSnapshot rawControllerSnapshot{};
    uint64_t frameId = 0;
    if (!smolnesRuntimeCopyLiveSnapshot(
            runtimeHandle_,
            reinterpret_cast<uint8_t*>(snapshot.videoFrame.pixels.data()),
            static_cast<uint32_t>(snapshot.videoFrame.pixels.size()),
            snapshot.paletteFrame.indices.data(),
            static_cast<uint32_t>(snapshot.paletteFrame.indices.size()),
            snapshot.memorySnapshot.cpuRam.data(),
            SMOLNES_RUNTIME_CPU_RAM_BYTES,
            snapshot.memorySnapshot.prgRam.data(),
            SMOLNES_RUNTIME_PRG_RAM_BYTES,
            &frameId,
            &rawControllerSnapshot)) {
        return std::nullopt;
    }

    snapshot.videoFrame.frame_id = frameId;
    snapshot.paletteFrame.frameId = frameId;
    snapshot.memorySnapshot.frameId = frameId;
    snapshot.controllerSnapshot = ControllerSnapshot{
        .latestFrameId = rawControllerSnapshot.latest_frame_id,
        .controller1AppliedFrameId = rawControllerSnapshot.controller1_applied_frame_id,
        .controller1ObservedTimestampNs = rawControllerSnapshot.controller1_observed_timestamp_ns,
        .controller1LatchTimestampNs = rawControllerSnapshot.controller1_latch_timestamp_ns,
        .controller1RequestTimestampNs = rawControllerSnapshot.controller1_request_timestamp_ns,
        .controller1SequenceId = rawControllerSnapshot.controller1_sequence_id,
        .controller1State = rawControllerSnapshot.controller1_state,
    };
    return snapshot;
}

std::optional<SmolnesRuntime::ControllerSnapshot> SmolnesRuntime::copyControllerSnapshot() const
{
    if (runtimeHandle_ == nullptr) {
        return std::nullopt;
    }

    SmolnesRuntimeControllerSnapshot raw{};
    if (!smolnesRuntimeCopyControllerSnapshot(runtimeHandle_, &raw)) {
        return std::nullopt;
    }

    return ControllerSnapshot{
        .latestFrameId = raw.latest_frame_id,
        .controller1AppliedFrameId = raw.controller1_applied_frame_id,
        .controller1ObservedTimestampNs = raw.controller1_observed_timestamp_ns,
        .controller1LatchTimestampNs = raw.controller1_latch_timestamp_ns,
        .controller1RequestTimestampNs = raw.controller1_request_timestamp_ns,
        .controller1SequenceId = raw.controller1_sequence_id,
        .controller1State = raw.controller1_state,
    };
}

std::optional<SmolnesRuntime::MemorySnapshot> SmolnesRuntime::copyMemorySnapshot() const
{
    if (runtimeHandle_ == nullptr) {
        return std::nullopt;
    }

    MemorySnapshot snapshot{};
    if (!smolnesRuntimeCopyMemorySnapshot(
            runtimeHandle_,
            snapshot.cpuRam.data(),
            SMOLNES_RUNTIME_CPU_RAM_BYTES,
            snapshot.prgRam.data(),
            SMOLNES_RUNTIME_PRG_RAM_BYTES,
            &snapshot.frameId)) {
        return std::nullopt;
    }
    return snapshot;
}

std::optional<SmolnesRuntime::ApuSnapshot> SmolnesRuntime::copyApuSnapshot() const
{
    if (runtimeHandle_ == nullptr) {
        return std::nullopt;
    }

    SmolnesApuSnapshot raw{};
    if (!smolnesRuntimeCopyApuSnapshot(runtimeHandle_, &raw)) {
        return std::nullopt;
    }

    ApuSnapshot snapshot{};
    snapshot.pulse1Enabled = raw.pulse1Enabled;
    snapshot.pulse2Enabled = raw.pulse2Enabled;
    snapshot.triangleEnabled = raw.triangleEnabled;
    snapshot.noiseEnabled = raw.noiseEnabled;
    snapshot.pulse1LengthCounter = raw.pulse1LengthCounter;
    snapshot.pulse2LengthCounter = raw.pulse2LengthCounter;
    snapshot.triangleLengthCounter = raw.triangleLengthCounter;
    snapshot.noiseLengthCounter = raw.noiseLengthCounter;
    snapshot.pulse1TimerPeriod = raw.pulse1TimerPeriod;
    snapshot.pulse2TimerPeriod = raw.pulse2TimerPeriod;
    snapshot.triangleTimerPeriod = raw.triangleTimerPeriod;
    snapshot.noiseTimerPeriod = raw.noiseTimerPeriod;
    snapshot.pulse1Duty = raw.pulse1Duty;
    snapshot.pulse2Duty = raw.pulse2Duty;
    snapshot.noiseMode = raw.noiseMode;
    snapshot.frameCounterMode5Step = raw.frameCounterMode5Step;
    snapshot.registerWriteCount = raw.registerWriteCount;
    snapshot.totalSamplesGenerated = raw.totalSamplesGenerated;
    return snapshot;
}

uint32_t SmolnesRuntime::copyApuSamples(float* buffer, uint32_t maxSamples) const
{
    if (runtimeHandle_ == nullptr) {
        return 0;
    }

    uint32_t samplesOut = 0;
    smolnesRuntimeCopyApuSamples(runtimeHandle_, buffer, maxSamples, &samplesOut);
    return samplesOut;
}

void SmolnesRuntime::setApuSampleCallback(SmolnesApuSampleCallback callback, void* userdata)
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeSetApuSampleCallback(runtimeHandle_, callback, userdata);
}

void SmolnesRuntime::setPacingMode(SmolnesRuntimePacingMode mode)
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeSetPacingMode(
        runtimeHandle_,
        mode == SmolnesRuntimePacingMode::Realtime ? SMOLNES_RUNTIME_PACING_MODE_REALTIME
                                                   : SMOLNES_RUNTIME_PACING_MODE_LOCKSTEP);
}

void SmolnesRuntime::setApuEnabled(bool enabled)
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeSetApuEnabled(runtimeHandle_, enabled);
}

void SmolnesRuntime::setDetailedTimingEnabled(bool enabled)
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeSetDetailedTimingEnabled(runtimeHandle_, enabled);
}

void SmolnesRuntime::setPixelOutputEnabled(bool enabled)
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeSetPixelOutputEnabled(runtimeHandle_, enabled);
}

void SmolnesRuntime::setRgbaOutputEnabled(bool enabled)
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeSetRgbaOutputEnabled(runtimeHandle_, enabled);
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
    snapshot.runtimeThreadApuStepMs = raw.runtime_thread_apu_step_ms;
    snapshot.runtimeThreadApuStepCalls = raw.runtime_thread_apu_step_calls;
    snapshot.runtimeThreadCpuStepMs = raw.runtime_thread_cpu_step_ms;
    snapshot.runtimeThreadCpuStepCalls = raw.runtime_thread_cpu_step_calls;
    snapshot.runtimeThreadFrameExecutionMs = raw.runtime_thread_frame_execution_ms;
    snapshot.runtimeThreadFrameExecutionCalls = raw.runtime_thread_frame_execution_calls;
    snapshot.runtimeThreadPpuStepMs = raw.runtime_thread_ppu_step_ms;
    snapshot.runtimeThreadPpuStepCalls = raw.runtime_thread_ppu_step_calls;
    snapshot.runtimeThreadPpuVisiblePixelsMs = raw.runtime_thread_ppu_visible_pixels_ms;
    snapshot.runtimeThreadPpuVisiblePixelsCalls = raw.runtime_thread_ppu_visible_pixels_calls;
    snapshot.runtimeThreadPpuSpriteEvalMs = raw.runtime_thread_ppu_sprite_eval_ms;
    snapshot.runtimeThreadPpuSpriteEvalCalls = raw.runtime_thread_ppu_sprite_eval_calls;
    snapshot.runtimeThreadPpuPostVisibleMs = raw.runtime_thread_ppu_post_visible_ms;
    snapshot.runtimeThreadPpuPostVisibleCalls = raw.runtime_thread_ppu_post_visible_calls;
    snapshot.runtimeThreadPpuPrefetchMs = raw.runtime_thread_ppu_prefetch_ms;
    snapshot.runtimeThreadPpuPrefetchCalls = raw.runtime_thread_ppu_prefetch_calls;
    snapshot.runtimeThreadPpuNonVisibleScanlinesMs =
        raw.runtime_thread_ppu_non_visible_scanlines_ms;
    snapshot.runtimeThreadPpuNonVisibleScanlinesCalls =
        raw.runtime_thread_ppu_non_visible_scanlines_calls;
    snapshot.runtimeThreadPpuOtherMs = raw.runtime_thread_ppu_other_ms;
    snapshot.runtimeThreadPpuOtherCalls = raw.runtime_thread_ppu_other_calls;
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
