#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"

#include "core/LoggingChannels.h"
#include "core/ScopeTimer.h"
#include "core/scenarios/nes/NesAudioPlayer.h"

#include <algorithm>
#include <limits>
#include <utility>

extern "C" void nesApuSampleCallback(float sample, void* userdata)
{
    static_cast<DirtSim::NesAudioPlayer*>(userdata)->pushSample(sample);
}

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

float axisValueFromMask(uint8_t mask, uint8_t negativeMask, uint8_t positiveMask)
{
    const bool negativePressed = (mask & negativeMask) != 0;
    const bool positivePressed = (mask & positiveMask) != 0;
    if (negativePressed == positivePressed) {
        return 0.0f;
    }
    return positivePressed ? 1.0f : -1.0f;
}

float buttonValueFromMask(uint8_t mask, uint8_t buttonMask)
{
    return (mask & buttonMask) != 0 ? 1.0f : 0.0f;
}

std::optional<NesControllerTelemetry> buildLiveControllerTelemetry(
    const SmolnesRuntime::ControllerSnapshot& snapshot)
{
    if (snapshot.controller1AppliedFrameId == 0 || snapshot.controller1SequenceId == 0
        || snapshot.controller1RequestTimestampNs == 0
        || snapshot.controller1LatchTimestampNs == 0) {
        return std::nullopt;
    }

    const uint8_t mask = snapshot.controller1State;
    return NesControllerTelemetry{
        .aRaw = buttonValueFromMask(mask, SMOLNES_RUNTIME_BUTTON_A),
        .bRaw = buttonValueFromMask(mask, SMOLNES_RUNTIME_BUTTON_B),
        .xRaw = axisValueFromMask(mask, SMOLNES_RUNTIME_BUTTON_LEFT, SMOLNES_RUNTIME_BUTTON_RIGHT),
        .yRaw = axisValueFromMask(mask, SMOLNES_RUNTIME_BUTTON_UP, SMOLNES_RUNTIME_BUTTON_DOWN),
        .inferredControllerMask = mask,
        .resolvedControllerMask = mask,
        .controllerSource = NesGameAdapterControllerSource::LiveInput,
        .controllerSourceFrameIndex = snapshot.controller1AppliedFrameId,
        .controllerAppliedFrameId = snapshot.controller1AppliedFrameId,
        .controllerObservedTimestampNs = snapshot.controller1ObservedTimestampNs > 0
            ? std::optional<uint64_t>(snapshot.controller1ObservedTimestampNs)
            : std::nullopt,
        .controllerLatchTimestampNs = snapshot.controller1LatchTimestampNs,
        .controllerRequestTimestampNs = snapshot.controller1RequestTimestampNs,
        .controllerSequenceId = snapshot.controller1SequenceId,
    };
}

} // namespace

NesSmolnesScenarioDriver::NesSmolnesScenarioDriver(Scenario::EnumType scenarioId)
    : NesSmolnesScenarioDriver(scenarioId, makeDefaultRuntimeConfig())
{}

NesSmolnesScenarioDriver::NesSmolnesScenarioDriver(
    Scenario::EnumType scenarioId, RuntimeConfig runtimeConfig)
    : scenarioId_(scenarioId),
      config_(makeDefaultConfig(scenarioId)),
      runtimeConfig_(std::move(runtimeConfig))
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
    lastControllerTelemetry_.reset();
    lastSmbResponseTelemetry_.reset();
    smbResponseProbe_.reset();
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
        if (runtimeConfig_.runtimeFactory) {
            runtime_ = runtimeConfig_.runtimeFactory();
        }
        else {
            runtime_ = std::make_unique<SmolnesRuntime>();
        }
    }
    if (!runtime_) {
        return Result<std::monostate, std::string>::error(
            "NesSmolnesScenarioDriver: Runtime factory returned null");
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

    if (audioPlaybackEnabled_ && !audioPlayer_) {
        audioPlayer_ = std::make_unique<NesAudioPlayer>();
        if (!audioPlayer_->start()) {
            LOG_WARN(Scenario, "NesSmolnesScenarioDriver: Audio playback failed to start.");
            audioPlayer_.reset();
        }
        else {
            runtime_->setApuSampleCallback(nesApuSampleCallback, audioPlayer_.get());
        }
    }
    applyRuntimePacingMode();

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> NesSmolnesScenarioDriver::reset()
{
    return setup();
}

NesSmolnesScenarioDriver::RuntimeConfig NesSmolnesScenarioDriver::makeDefaultRuntimeConfig()
{
    RuntimeConfig config;
    config.runtimeFactory = [] { return std::make_unique<SmolnesRuntime>(); };
    return config;
}

NesSmolnesScenarioDriver::StepResult NesSmolnesScenarioDriver::step(
    Timers& timers, uint8_t buttonMask)
{
    controller1State_ = buttonMask;
    lastSmbResponseTelemetry_.reset();

    StepResult stepResult;
    stepResult.controllerMask = controller1State_;
    stepResult.runtimeHealthy = runtime_ && runtime_->isHealthy();
    stepResult.runtimeRunning = runtime_ && runtime_->isRunning();
    stepResult.lastError = runtime_ ? runtime_->getLastError() : std::string{};
    if (!runtime_ || !stepResult.runtimeRunning) {
        return stepResult;
    }

    bool runtimeHealthy = false;
    {
        ScopeTimer healthTimer(timers, "nes_runtime_health_check");
        runtimeHealthy = runtime_->isHealthy();
    }
    stepResult.runtimeHealthy = runtimeHealthy;
    if (!runtimeHealthy) {
        LOG_ERROR(
            Scenario,
            "NesSmolnesScenarioDriver: smolnes runtime unhealthy for '{}': {}",
            toString(scenarioId_),
            runtime_->getLastError());
        stepResult.lastError = runtime_->getLastError();
        stopRuntime();
        stepResult.runtimeHealthy = false;
        stepResult.runtimeRunning = false;
        return stepResult;
    }

    {
        ScopeTimer renderedFramesTimer(timers, "nes_runtime_get_rendered_frame_count");
        stepResult.renderedFramesBefore = runtime_->getRenderedFrameCount();
    }

    uint32_t maxEpisodeFrames = 0;
    if (std::holds_alternative<Config::NesFlappyParatroopa>(config_)) {
        maxEpisodeFrames = getMaxEpisodeFrames(std::get<Config::NesFlappyParatroopa>(config_));
    }
    else if (std::holds_alternative<Config::NesSuperMarioBros>(config_)) {
        maxEpisodeFrames = getMaxEpisodeFrames(std::get<Config::NesSuperMarioBros>(config_));
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
        stepResult.runtimeHealthy = false;
        stepResult.runtimeRunning = false;
        stepResult.lastError = "Unsupported NES scenario config type";
        return stepResult;
    }
    if (stepResult.renderedFramesBefore >= maxEpisodeFrames) {
        return stepResult;
    }

    const uint64_t framesRemaining = maxEpisodeFrames - stepResult.renderedFramesBefore;

    {
        ScopeTimer setControllerTimer(timers, "nes_runtime_set_controller");
        runtime_->setController1State(controller1State_);
    }

    {
        const uint32_t framesToRun = static_cast<uint32_t>(std::min<uint64_t>(1u, framesRemaining));
        constexpr uint32_t tickTimeoutMs = 2000;
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
            stepResult.renderedFramesAfter = failureRenderedFrameCount;
            stepResult.lastError = runtime_->getLastError();
            stopRuntime();
            stepResult.runtimeHealthy = false;
            stepResult.runtimeRunning = false;
            return stepResult;
        }
    }

    {
        ScopeTimer renderedFramesTimer(timers, "nes_runtime_get_rendered_frame_count");
        stepResult.renderedFramesAfter = runtime_->getRenderedFrameCount();
    }
    if (stepResult.renderedFramesAfter > stepResult.renderedFramesBefore) {
        stepResult.advancedFrames =
            stepResult.renderedFramesAfter - stepResult.renderedFramesBefore;
    }

    if (stepResult.advancedFrames > 0) {
        {
            ScopeTimer copyFrameTimer(timers, "nes_runtime_copy_latest_frame");
            const auto liveSnapshot = runtime_->copyLiveSnapshot();
            if (liveSnapshot.has_value()) {
                stepResult.controllerTelemetry = liveSnapshot->controllerSnapshot.has_value()
                    ? buildLiveControllerTelemetry(liveSnapshot->controllerSnapshot.value())
                    : std::nullopt;
                stepResult.scenarioVideoFrame = liveSnapshot->videoFrame;
                stepResult.paletteFrame = liveSnapshot->paletteFrame;
                stepResult.memorySnapshot = liveSnapshot->memorySnapshot;
                if (liveSnapshot->videoFrame.frame_id > stepResult.renderedFramesAfter) {
                    stepResult.renderedFramesAfter = liveSnapshot->videoFrame.frame_id;
                    stepResult.advancedFrames =
                        stepResult.renderedFramesAfter - stepResult.renderedFramesBefore;
                }
            }
        }
        lastControllerTelemetry_ = stepResult.controllerTelemetry;
        if (smbResponseProbeEnabled_ && scenarioId_ == Scenario::EnumType::NesSuperMarioBros
            && stepResult.memorySnapshot.has_value()) {
            const uint64_t probeFrameId = stepResult.memorySnapshot->frameId > 0
                ? stepResult.memorySnapshot->frameId
                : stepResult.renderedFramesAfter;
            stepResult.smbResponseTelemetry = smbResponseProbe_.observeFrame(
                probeFrameId, stepResult.memorySnapshot.value(), stepResult.controllerTelemetry);
        }
    }
    lastSmbResponseTelemetry_ = stepResult.smbResponseTelemetry;

    updateRuntimeProfilingTimers(timers);
    stepResult.runtimeHealthy = runtime_ && runtime_->isHealthy();
    stepResult.runtimeRunning = runtime_ && runtime_->isRunning();
    stepResult.lastError = runtime_ ? runtime_->getLastError() : std::string{};
    return stepResult;
}

void NesSmolnesScenarioDriver::tick(
    Timers& timers, std::optional<ScenarioVideoFrame>& scenarioVideoFrame)
{
    StepResult stepResult = step(timers, controller1State_);
    if (stepResult.scenarioVideoFrame.has_value()) {
        scenarioVideoFrame = std::move(stepResult.scenarioVideoFrame);
        return;
    }
    if (!stepResult.runtimeHealthy || !stepResult.runtimeRunning) {
        scenarioVideoFrame.reset();
    }
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

std::optional<SmolnesRuntime::ApuSnapshot> NesSmolnesScenarioDriver::copyRuntimeApuSnapshot() const
{
    if (!runtime_ || !runtime_->isRunning() || !runtime_->isHealthy()) {
        return std::nullopt;
    }
    auto snapshot = runtime_->copyApuSnapshot();
    if (snapshot.has_value() && audioPlayer_) {
        const auto stats = audioPlayer_->getStats();
        snapshot->audioUnderruns = stats.underruns;
        snapshot->audioOverruns = stats.overruns;
        snapshot->audioCallbackCalls = stats.callbackCalls;
        snapshot->audioSamplesDropped = stats.samplesDropped;
    }
    return snapshot;
}

uint32_t NesSmolnesScenarioDriver::copyRuntimeApuSamples(float* buffer, uint32_t maxSamples) const
{
    if (!runtime_ || !runtime_->isRunning() || !runtime_->isHealthy()) {
        return 0;
    }
    return runtime_->copyApuSamples(buffer, maxSamples);
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

std::optional<NesControllerTelemetry> NesSmolnesScenarioDriver::getLastControllerTelemetry() const
{
    return lastControllerTelemetry_;
}

std::optional<NesSuperMarioBrosResponseTelemetry> NesSmolnesScenarioDriver::
    getLastSmbResponseTelemetry() const
{
    return lastSmbResponseTelemetry_;
}

void NesSmolnesScenarioDriver::setController1State(uint8_t buttonMask)
{
    controller1State_ = buttonMask;
    if (runtime_ && runtime_->isRunning()) {
        runtime_->setController1State(controller1State_);
    }
}

void NesSmolnesScenarioDriver::setLiveController1State(
    uint8_t buttonMask, uint64_t observedTimestampNs)
{
    controller1State_ = buttonMask;
    if (runtime_ && runtime_->isRunning()) {
        runtime_->setController1StateObserved(controller1State_, observedTimestampNs);
    }
}

void NesSmolnesScenarioDriver::setAudioPlaybackEnabled(bool enabled)
{
    audioPlaybackEnabled_ = enabled;
    if (enabled && runtime_ && runtime_->isRunning() && !audioPlayer_) {
        audioPlayer_ = std::make_unique<NesAudioPlayer>();
        if (!audioPlayer_->start()) {
            LOG_WARN(Scenario, "NesSmolnesScenarioDriver: Audio playback failed to start.");
            audioPlayer_.reset();
        }
        else {
            runtime_->setApuSampleCallback(nesApuSampleCallback, audioPlayer_.get());
        }
    }
    if (!enabled) {
        if (runtime_) {
            runtime_->setApuSampleCallback(nullptr, nullptr);
        }
        audioPlayer_.reset();
    }
    applyRuntimePacingMode();
}

void NesSmolnesScenarioDriver::setAudioVolumePercent(int percent)
{
    if (audioPlayer_) {
        audioPlayer_->setVolumePercent(percent);
    }
}

void NesSmolnesScenarioDriver::setLiveServerPacingEnabled(bool enabled)
{
    if (liveServerPacingEnabled_ == enabled) {
        return;
    }
    liveServerPacingEnabled_ = enabled;
    applyRuntimePacingMode();
}

void NesSmolnesScenarioDriver::setSmbResponseProbeEnabled(bool enabled)
{
    smbResponseProbeEnabled_ = enabled;
    if (!smbResponseProbeEnabled_) {
        lastSmbResponseTelemetry_.reset();
        smbResponseProbe_.reset();
    }
}

void NesSmolnesScenarioDriver::applyRuntimePacingMode()
{
    if (!runtime_ || !runtime_->isRunning()) {
        return;
    }

    const bool useRealtimePacing = audioPlayer_ && !liveServerPacingEnabled_;
    runtime_->setPacingMode(
        useRealtimePacing ? SmolnesRuntimePacingMode::Realtime
                          : SmolnesRuntimePacingMode::Lockstep);
}

void NesSmolnesScenarioDriver::stopRuntime()
{
    if (!runtime_) {
        return;
    }
    runtime_->setPacingMode(SmolnesRuntimePacingMode::Lockstep);
    runtime_->stop();
    audioPlayer_.reset();
    lastControllerTelemetry_.reset();
    lastSmbResponseTelemetry_.reset();
    lastRuntimeProfilingSnapshot_.reset();
    smbResponseProbe_.reset();
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
    if (std::holds_alternative<Config::NesSuperMarioBros>(config_)) {
        return validateSmolnesConfig(std::get<Config::NesSuperMarioBros>(config_));
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
