#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"

#include "core/LoggingChannels.h"
#include "core/ScopeTimer.h"

#include <algorithm>
#include <limits>

namespace DirtSim {

namespace {

uint32_t saturateCallCount(uint64_t count)
{
    if (count > std::numeric_limits<uint32_t>::max()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(count);
}

template <typename NesConfigT>
NesConfigValidationResult validateSmolnesConfig(const NesConfigT& config)
{
    return validateNesRomSelection(config.romId, config.romDirectory, config.romPath);
}

template <typename NesConfigT>
uint32_t getMaxEpisodeFrames(const NesConfigT& config)
{
    return config.maxEpisodeFrames;
}

} // namespace

NesSmolnesScenarioDriver::NesSmolnesScenarioDriver(Scenario::EnumType scenarioId)
    : scenarioId_(scenarioId), config_(makeDefaultConfig(scenarioId))
{}

NesSmolnesScenarioDriver::~NesSmolnesScenarioDriver()
{
    stopRuntime();
}

NesSmolnesScenarioDriver::NesSmolnesScenarioDriver(NesSmolnesScenarioDriver&&) noexcept = default;
NesSmolnesScenarioDriver& NesSmolnesScenarioDriver::operator=(NesSmolnesScenarioDriver&&) noexcept =
    default;

Result<std::monostate, std::string> NesSmolnesScenarioDriver::setConfig(
    const ScenarioConfig& config)
{
    const Scenario::EnumType incomingId = DirtSim::getScenarioId(config);
    if (incomingId != scenarioId_) {
        return Result<std::monostate, std::string>::error(
            "Scenario config mismatch for NES driver");
    }

    config_ = config;
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> NesSmolnesScenarioDriver::setup()
{
    stopRuntime();
    controller1State_ = 0;
    runtimeResolvedRomId_.clear();
    lastRuntimeProfilingSnapshot_.reset();

    const NesConfigValidationResult validation = validateConfig();
    lastRomCheck_ = validation.romCheck;
    if (!validation.valid) {
        LOG_ERROR(
            Scenario,
            "NesSmolnesScenarioDriver: ROM selection invalid for '{}' (mapper={}): {}",
            toString(scenarioId_),
            lastRomCheck_.mapper,
            validation.message);
        return Result<std::monostate, std::string>::error(validation.message);
    }

    runtimeResolvedRomId_ = validation.resolvedRomId;
    if (!runtime_) {
        runtime_ = std::make_unique<SmolnesRuntime>();
    }

    if (!runtime_->start(validation.resolvedRomPath.string())) {
        const std::string err = runtime_->getLastError();
        LOG_ERROR(
            Scenario,
            "NesSmolnesScenarioDriver: Failed to start smolnes runtime for '{}': {}",
            toString(scenarioId_),
            err);
        return Result<std::monostate, std::string>::error(err);
    }

    runtime_->setController1State(controller1State_);
    lastRuntimeProfilingSnapshot_ = runtime_->copyProfilingSnapshot();
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> NesSmolnesScenarioDriver::reset()
{
    return setup();
}

void NesSmolnesScenarioDriver::tick(
    Timers& timers, std::optional<ScenarioVideoFrame>& scenarioVideoFrame)
{
    if (!runtime_ || !runtime_->isRunning()) {
        return;
    }

    bool runtimeHealthy = false;
    {
        ScopeTimer healthTimer(timers, "nes_runtime_health_check");
        runtimeHealthy = runtime_->isHealthy();
    }
    if (!runtimeHealthy) {
        LOG_ERROR(
            Scenario,
            "NesSmolnesScenarioDriver: smolnes runtime unhealthy for '{}': {}",
            toString(scenarioId_),
            runtime_->getLastError());
        stopRuntime();
        return;
    }

    uint64_t renderedFrames = 0;
    {
        ScopeTimer renderedFramesTimer(timers, "nes_runtime_get_rendered_frame_count");
        renderedFrames = runtime_->getRenderedFrameCount();
    }

    uint32_t maxEpisodeFrames = 0;
    if (std::holds_alternative<Config::NesFlappyParatroopa>(config_)) {
        maxEpisodeFrames = getMaxEpisodeFrames(std::get<Config::NesFlappyParatroopa>(config_));
    }
    else if (std::holds_alternative<Config::NesSuperTiltBro>(config_)) {
        maxEpisodeFrames = getMaxEpisodeFrames(std::get<Config::NesSuperTiltBro>(config_));
    }
    else {
        LOG_ERROR(
            Scenario,
            "NesSmolnesScenarioDriver: Unsupported scenario config type for '{}'",
            toString(scenarioId_));
        stopRuntime();
        scenarioVideoFrame.reset();
        return;
    }
    if (renderedFrames >= maxEpisodeFrames) {
        return;
    }

    const uint64_t framesRemaining = maxEpisodeFrames - renderedFrames;
    const uint32_t framesToRun = static_cast<uint32_t>(std::min<uint64_t>(1u, framesRemaining));

    constexpr uint32_t tickTimeoutMs = 2000;
    {
        ScopeTimer setControllerTimer(timers, "nes_runtime_set_controller");
        runtime_->setController1State(controller1State_);
    }

    bool runFramesOk = false;
    {
        ScopeTimer runFramesTimer(timers, "nes_runtime_run_frames");
        runFramesOk = runtime_->runFrames(framesToRun, tickTimeoutMs);
    }
    if (!runFramesOk) {
        updateRuntimeProfilingTimers(timers);
        uint64_t failureRenderedFrameCount = 0;
        {
            ScopeTimer renderedFramesTimer(timers, "nes_runtime_get_rendered_frame_count");
            failureRenderedFrameCount = runtime_->getRenderedFrameCount();
        }
        LOG_ERROR(
            Scenario,
            "NesSmolnesScenarioDriver: smolnes frame step failed for '{}' after {} frames: {}",
            toString(scenarioId_),
            failureRenderedFrameCount,
            runtime_->getLastError());
        scenarioVideoFrame.reset();
        stopRuntime();
        return;
    }

    const bool hadScenarioFrame = scenarioVideoFrame.has_value();
    if (!hadScenarioFrame) {
        scenarioVideoFrame.emplace();
    }

    bool copiedFrame = false;
    {
        ScopeTimer copyFrameTimer(timers, "nes_runtime_copy_latest_frame");
        copiedFrame = runtime_->copyLatestFrameInto(scenarioVideoFrame.value());
    }
    if (!copiedFrame && !hadScenarioFrame) {
        scenarioVideoFrame.reset();
    }

    updateRuntimeProfilingTimers(timers);
}

bool NesSmolnesScenarioDriver::isRuntimeHealthy() const
{
    return runtime_ && runtime_->isHealthy();
}

bool NesSmolnesScenarioDriver::isRuntimeRunning() const
{
    return runtime_ && runtime_->isRunning();
}

uint64_t NesSmolnesScenarioDriver::getRuntimeRenderedFrameCount() const
{
    if (!runtime_) {
        return 0;
    }
    return runtime_->getRenderedFrameCount();
}

std::optional<ScenarioVideoFrame> NesSmolnesScenarioDriver::copyRuntimeFrameSnapshot() const
{
    if (!runtime_ || !runtime_->isRunning() || !runtime_->isHealthy()) {
        return std::nullopt;
    }
    return runtime_->copyLatestFrame();
}

std::optional<NesPaletteFrame> NesSmolnesScenarioDriver::copyRuntimePaletteFrame() const
{
    if (!runtime_ || !runtime_->isRunning() || !runtime_->isHealthy()) {
        return std::nullopt;
    }
    return runtime_->copyLatestPaletteFrame();
}

std::optional<SmolnesRuntime::MemorySnapshot> NesSmolnesScenarioDriver::copyRuntimeMemorySnapshot()
    const
{
    if (!runtime_ || !runtime_->isRunning() || !runtime_->isHealthy()) {
        return std::nullopt;
    }
    return runtime_->copyMemorySnapshot();
}

std::string NesSmolnesScenarioDriver::getRuntimeResolvedRomId() const
{
    return runtimeResolvedRomId_;
}

std::string NesSmolnesScenarioDriver::getRuntimeLastError() const
{
    if (!runtime_) {
        return {};
    }
    return runtime_->getLastError();
}

void NesSmolnesScenarioDriver::setController1State(uint8_t buttonMask)
{
    controller1State_ = buttonMask;
    if (runtime_ && runtime_->isRunning()) {
        runtime_->setController1State(controller1State_);
    }
}

void NesSmolnesScenarioDriver::stopRuntime()
{
    if (!runtime_) {
        return;
    }
    runtime_->stop();
    lastRuntimeProfilingSnapshot_.reset();
}

void NesSmolnesScenarioDriver::updateRuntimeProfilingTimers(Timers& timers)
{
    if (!runtime_) {
        return;
    }

    const auto snapshot = runtime_->copyProfilingSnapshot();
    if (!snapshot.has_value()) {
        return;
    }

    if (!lastRuntimeProfilingSnapshot_.has_value()) {
        lastRuntimeProfilingSnapshot_ = snapshot;
        return;
    }

    const auto& previous = lastRuntimeProfilingSnapshot_.value();
    const auto& current = snapshot.value();

    const auto addDelta = [&](const char* name,
                              double currentMs,
                              double previousMs,
                              uint64_t currentCalls,
                              uint64_t previousCalls) {
        if (currentMs < previousMs || currentCalls < previousCalls) {
            return;
        }

        const double deltaMs = currentMs - previousMs;
        const uint64_t deltaCalls = currentCalls - previousCalls;
        if (deltaMs <= 0.0 || deltaCalls == 0) {
            return;
        }

        timers.addSample(name, deltaMs, saturateCallCount(deltaCalls));
    };

    addDelta(
        "nes_runtime_runframes_wait",
        current.runFramesWaitMs,
        previous.runFramesWaitMs,
        current.runFramesWaitCalls,
        previous.runFramesWaitCalls);
    addDelta(
        "nes_runtime_thread_idle_wait",
        current.runtimeThreadIdleWaitMs,
        previous.runtimeThreadIdleWaitMs,
        current.runtimeThreadIdleWaitCalls,
        previous.runtimeThreadIdleWaitCalls);
    addDelta(
        "nes_runtime_thread_cpu_step",
        current.runtimeThreadCpuStepMs,
        previous.runtimeThreadCpuStepMs,
        current.runtimeThreadCpuStepCalls,
        previous.runtimeThreadCpuStepCalls);
    addDelta(
        "nes_runtime_thread_frame_execution",
        current.runtimeThreadFrameExecutionMs,
        previous.runtimeThreadFrameExecutionMs,
        current.runtimeThreadFrameExecutionCalls,
        previous.runtimeThreadFrameExecutionCalls);
    addDelta(
        "nes_runtime_thread_ppu_step",
        current.runtimeThreadPpuStepMs,
        previous.runtimeThreadPpuStepMs,
        current.runtimeThreadPpuStepCalls,
        previous.runtimeThreadPpuStepCalls);
    addDelta(
        "nes_runtime_thread_ppu_visible_pixels",
        current.runtimeThreadPpuVisiblePixelsMs,
        previous.runtimeThreadPpuVisiblePixelsMs,
        current.runtimeThreadPpuVisiblePixelsCalls,
        previous.runtimeThreadPpuVisiblePixelsCalls);
    addDelta(
        "nes_runtime_thread_ppu_sprite_eval",
        current.runtimeThreadPpuSpriteEvalMs,
        previous.runtimeThreadPpuSpriteEvalMs,
        current.runtimeThreadPpuSpriteEvalCalls,
        previous.runtimeThreadPpuSpriteEvalCalls);
    addDelta(
        "nes_runtime_thread_ppu_prefetch",
        current.runtimeThreadPpuPrefetchMs,
        previous.runtimeThreadPpuPrefetchMs,
        current.runtimeThreadPpuPrefetchCalls,
        previous.runtimeThreadPpuPrefetchCalls);
    addDelta(
        "nes_runtime_thread_ppu_other",
        current.runtimeThreadPpuOtherMs,
        previous.runtimeThreadPpuOtherMs,
        current.runtimeThreadPpuOtherCalls,
        previous.runtimeThreadPpuOtherCalls);
    addDelta(
        "nes_runtime_thread_frame_submit",
        current.runtimeThreadFrameSubmitMs,
        previous.runtimeThreadFrameSubmitMs,
        current.runtimeThreadFrameSubmitCalls,
        previous.runtimeThreadFrameSubmitCalls);
    addDelta(
        "nes_runtime_thread_event_poll",
        current.runtimeThreadEventPollMs,
        previous.runtimeThreadEventPollMs,
        current.runtimeThreadEventPollCalls,
        previous.runtimeThreadEventPollCalls);
    addDelta(
        "nes_runtime_thread_present",
        current.runtimeThreadPresentMs,
        previous.runtimeThreadPresentMs,
        current.runtimeThreadPresentCalls,
        previous.runtimeThreadPresentCalls);
    addDelta(
        "nes_runtime_memory_snapshot_copy",
        current.memorySnapshotCopyMs,
        previous.memorySnapshotCopyMs,
        current.memorySnapshotCopyCalls,
        previous.memorySnapshotCopyCalls);

    lastRuntimeProfilingSnapshot_ = snapshot;
}

NesConfigValidationResult NesSmolnesScenarioDriver::validateConfig() const
{
    if (std::holds_alternative<Config::NesFlappyParatroopa>(config_)) {
        return validateSmolnesConfig(std::get<Config::NesFlappyParatroopa>(config_));
    }
    if (std::holds_alternative<Config::NesSuperTiltBro>(config_)) {
        return validateSmolnesConfig(std::get<Config::NesSuperTiltBro>(config_));
    }

    NesConfigValidationResult validation{};
    validation.valid = false;
    validation.message = "Unsupported NES scenario config type";
    validation.romCheck.status = NesRomCheckStatus::FileNotFound;
    validation.romCheck.message = validation.message;
    return validation;
}

} // namespace DirtSim
