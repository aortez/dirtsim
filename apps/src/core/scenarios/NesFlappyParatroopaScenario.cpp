#include "NesFlappyParatroopaScenario.h"

#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/ScopeTimer.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <system_error>
#include <utility>

namespace {

const char* romCheckStatusToString(DirtSim::NesRomCheckStatus status)
{
    switch (status) {
        case DirtSim::NesRomCheckStatus::Compatible:
            return "compatible";
        case DirtSim::NesRomCheckStatus::FileNotFound:
            return "file_not_found";
        case DirtSim::NesRomCheckStatus::InvalidHeader:
            return "invalid_header";
        case DirtSim::NesRomCheckStatus::ReadError:
            return "read_error";
        case DirtSim::NesRomCheckStatus::UnsupportedMapper:
            return "unsupported_mapper";
    }
    return "unknown";
}

uint32_t saturateCallCount(uint64_t count)
{
    if (count > std::numeric_limits<uint32_t>::max()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(count);
}

std::string describeRomSource(const DirtSim::Config::NesFlappyParatroopa& config)
{
    if (!config.romId.empty()) {
        return "romId '" + config.romId + "'";
    }
    return "romPath '" + config.romPath + "'";
}

} // namespace

namespace DirtSim {

NesFlappyParatroopaScenario::NesFlappyParatroopaScenario()
{
    metadata_.kind = ScenarioKind::NesWorld;
    metadata_.name = "NES Flappy Paratroopa";
    metadata_.description = "NES Flappy Paratroopa World training scenario";
    metadata_.category = "organisms";
    metadata_.requiredWidth = 47;
    metadata_.requiredHeight = 30;
    runtime_ = std::make_unique<SmolnesRuntime>();
}

NesFlappyParatroopaScenario::~NesFlappyParatroopaScenario()
{
    stopRuntime();
}

const ScenarioMetadata& NesFlappyParatroopaScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig NesFlappyParatroopaScenario::getConfig() const
{
    return config_;
}

void NesFlappyParatroopaScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    if (!std::holds_alternative<Config::NesFlappyParatroopa>(newConfig)) {
        LOG_ERROR(Scenario, "NesFlappyParatroopaScenario: Invalid config type provided");
        return;
    }

    config_ = std::get<Config::NesFlappyParatroopa>(newConfig);
    LOG_INFO(Scenario, "NesFlappyParatroopaScenario: Config updated");
}

void NesFlappyParatroopaScenario::setup(World& world)
{
    stopRuntime();
    world.getData().scenario_video_frame.reset();
    controller1State_ = 0;
    runtimeResolvedRomId_.clear();
    lastRuntimeProfilingSnapshot_.reset();

    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell();
        }
    }
    world.getOrganismManager().clear();

    for (int x = 0; x < world.getData().width; ++x) {
        world.getData()
            .at(x, world.getData().height - 1)
            .replaceMaterial(Material::EnumType::Wall, 1.0);
    }
    for (int y = 0; y < world.getData().height; ++y) {
        world.getData().at(0, y).replaceMaterial(Material::EnumType::Wall, 1.0);
        world.getData()
            .at(world.getData().width - 1, y)
            .replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    const NesConfigValidationResult validation = validateConfig(config_);
    lastRomCheck_ = validation.romCheck;
    if (!validation.valid) {
        const char* statusText = romCheckStatusToString(lastRomCheck_.status);
        LOG_ERROR(
            Scenario,
            "NesFlappyParatroopaScenario: {} invalid ({}, mapper={}): {}",
            describeRomSource(config_),
            statusText,
            lastRomCheck_.mapper,
            validation.message);
        return;
    }

    LOG_INFO(
        Scenario,
        "NesFlappyParatroopaScenario: ROM '{}' compatible (id='{}', mapper={}, prg16k={}, "
        "chr8k={})",
        validation.resolvedRomPath.string(),
        validation.resolvedRomId,
        lastRomCheck_.mapper,
        static_cast<uint32_t>(lastRomCheck_.prgBanks16k),
        static_cast<uint32_t>(lastRomCheck_.chrBanks8k));
    runtimeResolvedRomId_ = validation.resolvedRomId;
    if (!runtime_) {
        runtime_ = std::make_unique<SmolnesRuntime>();
    }

    if (!runtime_->start(validation.resolvedRomPath.string())) {
        LOG_ERROR(
            Scenario,
            "NesFlappyParatroopaScenario: Failed to start smolnes runtime: {}",
            runtime_->getLastError());
    }
    else {
        runtime_->setController1State(controller1State_);
        lastRuntimeProfilingSnapshot_ = runtime_->copyProfilingSnapshot();
    }
}

void NesFlappyParatroopaScenario::reset(World& world)
{
    setup(world);
}

void NesFlappyParatroopaScenario::tick(World& world, double /*deltaTime*/)
{
    if (!runtime_ || !runtime_->isRunning()) {
        return;
    }

    Timers& timers = world.getTimers();

    bool runtimeHealthy = false;
    {
        ScopeTimer healthTimer(timers, "nes_runtime_health_check");
        runtimeHealthy = runtime_->isHealthy();
    }
    if (!runtimeHealthy) {
        LOG_ERROR(
            Scenario,
            "NesFlappyParatroopaScenario: smolnes runtime unhealthy: {}",
            runtime_->getLastError());
        stopRuntime();
        return;
    }

    uint64_t renderedFrames = 0;
    {
        ScopeTimer renderedFramesTimer(timers, "nes_runtime_get_rendered_frame_count");
        renderedFrames = runtime_->getRenderedFrameCount();
    }
    if (renderedFrames >= config_.maxEpisodeFrames) {
        return;
    }

    const uint64_t framesRemaining = config_.maxEpisodeFrames - renderedFrames;
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
            "NesFlappyParatroopaScenario: smolnes frame step failed after {} frames: {}",
            failureRenderedFrameCount,
            runtime_->getLastError());
        world.getData().scenario_video_frame.reset();
        stopRuntime();
        return;
    }

    auto& scenarioFrame = world.getData().scenario_video_frame;
    const bool hadScenarioFrame = scenarioFrame.has_value();
    if (!hadScenarioFrame) {
        scenarioFrame.emplace();
    }
    bool copiedFrame = false;
    {
        ScopeTimer copyFrameTimer(timers, "nes_runtime_copy_latest_frame");
        copiedFrame = runtime_->copyLatestFrameInto(scenarioFrame.value());
    }
    if (!copiedFrame && !hadScenarioFrame) {
        scenarioFrame.reset();
    }

    updateRuntimeProfilingTimers(timers);
}

const NesRomCheckResult& NesFlappyParatroopaScenario::getLastRomCheck() const
{
    return lastRomCheck_;
}

bool NesFlappyParatroopaScenario::isRuntimeHealthy() const
{
    return runtime_ && runtime_->isHealthy();
}

bool NesFlappyParatroopaScenario::isRuntimeRunning() const
{
    return runtime_ && runtime_->isRunning();
}

uint64_t NesFlappyParatroopaScenario::getRuntimeRenderedFrameCount() const
{
    if (!runtime_) {
        return 0;
    }
    return runtime_->getRenderedFrameCount();
}

std::optional<ScenarioVideoFrame> NesFlappyParatroopaScenario::copyRuntimeFrameSnapshot() const
{
    if (!runtime_ || !runtime_->isRunning() || !runtime_->isHealthy()) {
        return std::nullopt;
    }
    return runtime_->copyLatestFrame();
}

std::optional<NesPaletteFrame> NesFlappyParatroopaScenario::copyRuntimePaletteFrame() const
{
    if (!runtime_ || !runtime_->isRunning() || !runtime_->isHealthy()) {
        return std::nullopt;
    }
    return runtime_->copyLatestPaletteFrame();
}

std::string NesFlappyParatroopaScenario::getRuntimeResolvedRomId() const
{
    return runtimeResolvedRomId_;
}

std::string NesFlappyParatroopaScenario::getRuntimeLastError() const
{
    if (!runtime_) {
        return {};
    }
    return runtime_->getLastError();
}

std::optional<SmolnesRuntime::MemorySnapshot> NesFlappyParatroopaScenario::
    copyRuntimeMemorySnapshot() const
{
    if (!runtime_ || !runtime_->isRunning() || !runtime_->isHealthy()) {
        return std::nullopt;
    }
    return runtime_->copyMemorySnapshot();
}

void NesFlappyParatroopaScenario::setController1State(uint8_t buttonMask)
{
    controller1State_ = buttonMask;
    if (runtime_ && runtime_->isRunning()) {
        runtime_->setController1State(controller1State_);
    }
}

std::vector<NesRomCatalogEntry> NesFlappyParatroopaScenario::scanRomCatalog(
    const std::filesystem::path& romDir)
{
    return scanNesRomCatalog(romDir);
}

std::string NesFlappyParatroopaScenario::makeRomId(const std::string& rawName)
{
    return makeNesRomId(rawName);
}

NesConfigValidationResult NesFlappyParatroopaScenario::validateConfig(
    const Config::NesFlappyParatroopa& config)
{
    return validateNesRomSelection(config.romId, config.romDirectory, config.romPath);
}

NesRomCheckResult NesFlappyParatroopaScenario::inspectRom(const std::filesystem::path& romPath)
{
    return inspectNesRom(romPath);
}

bool NesFlappyParatroopaScenario::isMapperSupportedBySmolnes(uint16_t mapper)
{
    return isNesMapperSupportedBySmolnes(mapper);
}

void NesFlappyParatroopaScenario::stopRuntime()
{
    if (!runtime_) {
        return;
    }
    runtime_->stop();
    lastRuntimeProfilingSnapshot_.reset();
}

void NesFlappyParatroopaScenario::updateRuntimeProfilingTimers(Timers& timers)
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
        "nes_runtime_thread_ppu_background_pipeline",
        current.runtimeThreadPpuVisiblePixelsMs,
        previous.runtimeThreadPpuVisiblePixelsMs,
        current.runtimeThreadPpuVisiblePixelsCalls,
        previous.runtimeThreadPpuVisiblePixelsCalls);
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

} // namespace DirtSim
