#include "TrainingActive.h"
#include "StartMenu.h"
#include "State.h"
#include "TrainingUnsavedResult.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/EvolutionStop.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/TrainingStreamConfigSet.h"
#include "server/api/UserSettingsPatch.h"
#include "ui/TrainingActiveView.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace DirtSim {
namespace Ui {
namespace State {
namespace {
constexpr size_t plotRefreshPointCount = 120;

Result<Api::TrainingStreamConfigSet::OkayType, std::string> sendTrainingStreamConfig(
    StateMachine& sm,
    int intervalMs,
    bool bestPlaybackEnabled,
    int bestPlaybackIntervalMs,
    int timeoutMs = 2000)
{
    if (!sm.hasWebSocketService()) {
        return Result<Api::TrainingStreamConfigSet::OkayType, std::string>::error(
            "No WebSocketService available");
    }

    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        return Result<Api::TrainingStreamConfigSet::OkayType, std::string>::error(
            "Not connected to server");
    }

    Api::TrainingStreamConfigSet::Command cmd{
        .intervalMs = intervalMs,
        .bestPlaybackEnabled = bestPlaybackEnabled,
        .bestPlaybackIntervalMs = bestPlaybackIntervalMs,
    };
    const auto result =
        wsService.sendCommandAndGetResponse<Api::TrainingStreamConfigSet::OkayType>(cmd, timeoutMs);
    if (result.isError()) {
        return Result<Api::TrainingStreamConfigSet::OkayType, std::string>::error(
            result.errorValue());
    }
    if (result.value().isError()) {
        return Result<Api::TrainingStreamConfigSet::OkayType, std::string>::error(
            result.value().errorValue().message);
    }

    return Result<Api::TrainingStreamConfigSet::OkayType, std::string>::okay(
        result.value().value());
}

GenomeId getBestGenomeId(const std::vector<Api::TrainingResult::Candidate>& candidates)
{
    if (candidates.empty()) {
        return INVALID_GENOME_ID;
    }

    const auto bestIt =
        std::max_element(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a.fitness < b.fitness;
        });
    return bestIt != candidates.end() ? bestIt->id : INVALID_GENOME_ID;
}

void beginEvolutionSession(TrainingActive& state, StateMachine& sm)
{
    state.hasPlottedRobustBestFitness_ = false;
    state.plottedRobustBestFitness_ = 0.0f;
    state.plotBestSeries_.clear();
    state.plotBestSeriesRobustHighMask_.clear();

    state.plotBestSeries_.push_back(0.0f);
    state.plotBestSeriesRobustHighMask_.push_back(0);

    state.lastPlottedRobustEvaluationCount_ = 0;
    state.lastPlottedCompletedGeneration_ = -1;
    state.trainingPaused_ = false;
    state.progressEventCount_ = 0;
    state.renderMessageCount_ = 0;
    state.lastRenderRateLog_ = std::chrono::steady_clock::now();
    state.uiLoopCount_ = 0;
    state.lastUiLoopLog_ = std::chrono::steady_clock::now();
    state.lastProgressRateLog_ = std::chrono::steady_clock::now();

    DIRTSIM_ASSERT(state.view_, "TrainingActiveView must exist");
    state.view_->setEvolutionStarted(true);
    state.view_->setTrainingPaused(false);
    state.view_->updateFitnessPlots(state.plotBestSeries_, state.plotBestSeriesRobustHighMask_);

    // Stream setup is also done in TrainingIdle before EvolutionStart to prevent a deadlock
    // when training completes quickly. This second call handles the restart-from-unsaved-result
    // path where TrainingIdle is skipped.
    if (!sm.hasWebSocketService()) {
        LOG_WARN(State, "No WebSocketService available for training stream setup");
        return;
    }

    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, cannot setup training streams");
        return;
    }

    constexpr int startupStreamSetupTimeoutMs = 250;
    const auto& settings = sm.getUserSettings();
    const auto streamResult = sendTrainingStreamConfig(
        sm,
        settings.streamIntervalMs,
        settings.bestPlaybackEnabled,
        settings.bestPlaybackIntervalMs,
        startupStreamSetupTimeoutMs);
    if (streamResult.isError()) {
        LOG_WARN(
            State,
            "TrainingStreamConfigSet failed (intervalMs={}, bestPlaybackEnabled={}, "
            "bestPlaybackIntervalMs={}): {}",
            settings.streamIntervalMs,
            settings.bestPlaybackEnabled,
            settings.bestPlaybackIntervalMs,
            streamResult.errorValue());
    }
    else {
        LOG_INFO(
            State,
            "Training stream config set (interval={}ms, bestPlaybackEnabled={}, "
            "bestPlaybackInterval={}ms)",
            streamResult.value().intervalMs,
            streamResult.value().bestPlaybackEnabled,
            streamResult.value().bestPlaybackIntervalMs);
    }

    Api::RenderFormatSet::Command renderCmd;
    renderCmd.format = RenderFormat::EnumType::Basic;
    auto envelope =
        DirtSim::Network::make_command_envelope(wsService.allocateRequestId(), renderCmd);
    auto renderResult = wsService.sendBinaryAndReceive(envelope, startupStreamSetupTimeoutMs);
    if (renderResult.isError()) {
        LOG_WARN(State, "Failed to subscribe to render stream: {}", renderResult.errorValue());
    }
    else {
        LOG_INFO(State, "Subscribed to render stream for live training view");
    }

    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->hide();
            panel->resetWidth();
        }
        if (auto* iconRail = uiManager->getIconRail()) {
            iconRail->deselectAll();
        }
    }
}

} // namespace

TrainingActive::TrainingActive(
    TrainingSpec lastTrainingSpec,
    bool hasTrainingSpec,
    std::optional<Starfield::Snapshot> starfieldSnapshot)
    : lastTrainingSpec_(std::move(lastTrainingSpec)),
      hasTrainingSpec_(hasTrainingSpec),
      starfieldSnapshot_(std::move(starfieldSnapshot))
{}

TrainingActive::~TrainingActive() = default;

void TrainingActive::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Training active state");

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    DirtSim::Network::WebSocketServiceInterface* wsService = nullptr;
    if (sm.hasWebSocketService()) {
        wsService = &sm.getWebSocketService();
    }

    view_ = std::make_unique<TrainingActiveView>(
        uiManager,
        sm,
        wsService,
        sm.getUserSettings(),
        starfieldSnapshot_ ? &starfieldSnapshot_.value() : nullptr);
    DIRTSIM_ASSERT(view_, "TrainingActiveView creation failed");

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setVisible(false);

    if (auto* panel = uiManager->getExpandablePanel()) {
        panel->clearContent();
        panel->hide();
        panel->resetWidth();
    }

    beginEvolutionSession(*this, sm);
}

void TrainingActive::onExit(StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exiting Training active state");
}

void TrainingActive::updateAnimations()
{
    const auto now = std::chrono::steady_clock::now();
    if (lastUiLoopLog_ == std::chrono::steady_clock::time_point{}) {
        lastUiLoopLog_ = now;
        uiLoopCount_ = 0;
    }

    uiLoopCount_++;
    const auto elapsed = now - lastUiLoopLog_;
    if (elapsed >= std::chrono::seconds(1)) {
        const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
        const double rate = elapsedSeconds > 0.0 ? (uiLoopCount_ / elapsedSeconds) : 0.0;
        LOG_INFO(State, "Training UI loop FPS: {:.1f}", rate);
        uiLoopCount_ = 0;
        lastUiLoopLog_ = now;
    }

    DIRTSIM_ASSERT(view_, "TrainingActiveView must exist");
    view_->updateAnimations();
}

bool TrainingActive::isTrainingResultModalVisible() const
{
    DIRTSIM_ASSERT(view_, "TrainingActiveView must exist");
    return view_->isTrainingResultModalVisible();
}

State::Any TrainingActive::onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& /*sm*/)
{
    progress = evt.progress;
    progressEventCount_++;

    const auto now = std::chrono::steady_clock::now();
    if (lastProgressRateLog_ == std::chrono::steady_clock::time_point{}) {
        lastProgressRateLog_ = now;
        progressEventCount_ = 0;
    }

    const auto elapsed = now - lastProgressRateLog_;
    if (elapsed >= std::chrono::seconds(10)) {
        const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
        const double rate = elapsedSeconds > 0.0 ? (progressEventCount_ / elapsedSeconds) : 0.0;
        LOG_INFO(State, "Training progress rate: {:.1f} msgs/s", rate);
        progressEventCount_ = 0;
        lastProgressRateLog_ = now;
    }

    LOG_DEBUG(
        State,
        "Evolution progress: gen {}/{}, eval {}/{}, best fitness {:.2f}",
        progress.generation,
        progress.maxGenerations,
        progress.currentEval,
        progress.populationSize,
        progress.bestFitnessAllTime);

    DIRTSIM_ASSERT(view_, "TrainingActiveView must exist");
    view_->updateProgress(progress);

    const bool robustSampleAppended =
        progress.robustEvaluationCount > lastPlottedRobustEvaluationCount_;
    const bool nonRobustGenerationCompleted = progress.robustEvaluationCount == 0
        && progress.lastCompletedGeneration >= 0
        && progress.lastCompletedGeneration > lastPlottedCompletedGeneration_
        && progress.bestThisGenSource != "none";

    if (robustSampleAppended) {
        lastPlottedRobustEvaluationCount_ = progress.robustEvaluationCount;
    }
    if (nonRobustGenerationCompleted) {
        lastPlottedCompletedGeneration_ = progress.lastCompletedGeneration;
    }

    if (robustSampleAppended || nonRobustGenerationCompleted) {
        const float plottedValue = static_cast<float>(progress.bestFitnessThisGen);
        plotBestSeries_.push_back(plottedValue);

        bool isNewRobustHigh = false;
        constexpr float robustFitnessEpsilon = 0.0001f;
        if (robustSampleAppended) {
            if (!hasPlottedRobustBestFitness_
                || plottedValue > plottedRobustBestFitness_ + robustFitnessEpsilon) {
                hasPlottedRobustBestFitness_ = true;
                plottedRobustBestFitness_ = plottedValue;
                isNewRobustHigh = true;
            }
        }
        plotBestSeriesRobustHighMask_.push_back(isNewRobustHigh ? 1 : 0);
        if (plotBestSeries_.size() > plotRefreshPointCount) {
            const size_t pruneCount = plotBestSeries_.size() - plotRefreshPointCount;
            plotBestSeries_.erase(plotBestSeries_.begin(), plotBestSeries_.begin() + pruneCount);
            if (plotBestSeriesRobustHighMask_.size() > pruneCount) {
                plotBestSeriesRobustHighMask_.erase(
                    plotBestSeriesRobustHighMask_.begin(),
                    plotBestSeriesRobustHighMask_.begin() + pruneCount);
            }
            else {
                plotBestSeriesRobustHighMask_.clear();
            }
        }

        view_->updateFitnessPlots(plotBestSeries_, plotBestSeriesRobustHighMask_);
    }

    return std::move(*this);
}

State::Any TrainingActive::onEvent(
    const TrainingBestPlaybackFrameReceivedEvent& evt, StateMachine& /*sm*/)
{
    DIRTSIM_ASSERT(view_, "TrainingActiveView must exist");

    WorldData worldData = evt.frame.worldData;
    worldData.scenario_video_frame = evt.frame.scenarioVideoFrame;
    worldData.organism_ids = evt.frame.organismIds;
    view_->updateBestPlaybackFrame(worldData, evt.frame.fitness, evt.frame.generation);
    return std::move(*this);
}

State::Any TrainingActive::onEvent(
    const TrainingBestSnapshotReceivedEvent& evt, StateMachine& /*sm*/)
{
    DIRTSIM_ASSERT(view_, "TrainingActiveView must exist");

    WorldData worldData = evt.snapshot.worldData;
    worldData.scenario_video_frame = evt.snapshot.scenarioVideoFrame;
    worldData.organism_ids = evt.snapshot.organismIds;
    LOG_INFO(
        State,
        "Training best snapshot received: fitness={:.4f} gen={} world={}x{} cells={} colors={} "
        "organism_ids={} accepted={} rejected={} signatures={} outcome_signatures={}",
        evt.snapshot.fitness,
        evt.snapshot.generation,
        worldData.width,
        worldData.height,
        worldData.cells.size(),
        worldData.colors.size(),
        worldData.organism_ids.size(),
        evt.snapshot.commandsAccepted,
        evt.snapshot.commandsRejected,
        evt.snapshot.topCommandSignatures.size(),
        evt.snapshot.topCommandOutcomeSignatures.size());

    std::vector<std::pair<std::string, int>> topCommandSignatures;
    topCommandSignatures.reserve(evt.snapshot.topCommandSignatures.size());
    for (const auto& entry : evt.snapshot.topCommandSignatures) {
        topCommandSignatures.emplace_back(entry.signature, entry.count);
    }
    std::vector<std::pair<std::string, int>> topCommandOutcomeSignatures;
    topCommandOutcomeSignatures.reserve(evt.snapshot.topCommandOutcomeSignatures.size());
    for (const auto& entry : evt.snapshot.topCommandOutcomeSignatures) {
        topCommandOutcomeSignatures.emplace_back(entry.signature, entry.count);
    }
    view_->updateBestSnapshot(
        worldData,
        evt.snapshot.fitness,
        evt.snapshot.generation,
        evt.snapshot.commandsAccepted,
        evt.snapshot.commandsRejected,
        topCommandSignatures,
        topCommandOutcomeSignatures);

    return std::move(*this);
}

State::Any TrainingActive::onEvent(const Api::TrainingResult::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Training result available (candidates={})", cwc.command.candidates.size());

    trainingPaused_ = false;
    const GenomeId bestGenomeId = getBestGenomeId(cwc.command.candidates);

    DIRTSIM_ASSERT(view_, "TrainingActiveView must exist");
    view_->setEvolutionCompleted(bestGenomeId);
    view_->setTrainingPaused(false);

    cwc.sendResponse(Api::TrainingResult::Response::okay(std::monostate{}));

    starfieldSnapshot_ = view_->captureStarfieldSnapshot();
    return TrainingUnsavedResult{
        lastTrainingSpec_,      hasTrainingSpec_,   cwc.command.summary,
        cwc.command.candidates, starfieldSnapshot_,
    };
}

State::Any TrainingActive::onEvent(const IconSelectedEvent& evt, StateMachine& /*sm*/)
{
    LOG_INFO(
        State,
        "Icon selection ignored during active training: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));
    return std::move(*this);
}

State::Any TrainingActive::onEvent(const StopTrainingClickedEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Stop button clicked, stopping evolution");

    trainingPaused_ = false;

    if (!sm.hasWebSocketService()) {
        LOG_ERROR(State, "No WebSocketService available");
        return StartMenu{};
    }
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, cannot stop evolution");
        return StartMenu{};
    }

    Api::EvolutionStop::Command cmd;
    const auto result =
        wsService.sendCommandAndGetResponse<Api::EvolutionStop::OkayType>(cmd, 2000);
    if (result.isError()) {
        LOG_ERROR(State, "Failed to send EvolutionStop: {}", result.errorValue());
    }
    else if (result.value().isError()) {
        LOG_ERROR(State, "Server EvolutionStop error: {}", result.value().errorValue().message);
    }
    else {
        LOG_INFO(State, "Evolution stopped on server");
    }

    return StartMenu{};
}

State::Any TrainingActive::onEvent(const QuitTrainingClickedEvent& /*evt*/, StateMachine& sm)
{
    return onEvent(StopTrainingClickedEvent{}, sm);
}

State::Any TrainingActive::onEvent(
    const TrainingPauseResumeClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    trainingPaused_ = !trainingPaused_;
    DIRTSIM_ASSERT(view_, "TrainingActiveView must exist");
    view_->setTrainingPaused(trainingPaused_);

    LOG_INFO(State, "Training pause toggled: {}", trainingPaused_);
    return std::move(*this);
}

State::Any TrainingActive::onEvent(const TrainingConfigUpdatedEvent& evt, StateMachine& sm)
{
    auto& localSettings = sm.getUserSettings();
    localSettings.trainingSpec = evt.training;
    localSettings.evolutionConfig = evt.evolution;
    localSettings.mutationConfig = evt.mutation;

    if (!sm.hasWebSocketService()) {
        return std::move(*this);
    }

    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        return std::move(*this);
    }

    Api::UserSettingsPatch::Command patchCmd{
        .trainingSpec = evt.training,
        .evolutionConfig = evt.evolution,
        .mutationConfig = evt.mutation,
    };
    const auto patchResult =
        wsService.sendCommandAndGetResponse<Api::UserSettingsPatch::Okay>(patchCmd, 2000);
    if (patchResult.isError()) {
        LOG_WARN(
            State, "UserSettingsPatch failed for training config: {}", patchResult.errorValue());
        return std::move(*this);
    }
    if (patchResult.value().isError()) {
        LOG_WARN(
            State,
            "UserSettingsPatch rejected for training config: {}",
            patchResult.value().errorValue().message);
        return std::move(*this);
    }

    sm.syncTrainingUserSettings(patchResult.value().value().settings);
    return std::move(*this);
}

State::Any TrainingActive::onEvent(const TrainingStreamConfigChangedEvent& evt, StateMachine& sm)
{
    auto& settings = sm.getUserSettings();
    settings.streamIntervalMs = std::max(0, evt.intervalMs);
    settings.bestPlaybackEnabled = evt.bestPlaybackEnabled;
    settings.bestPlaybackIntervalMs = std::max(1, evt.bestPlaybackIntervalMs);

    DIRTSIM_ASSERT(view_, "TrainingActiveView must exist");
    view_->setStreamIntervalMs(settings.streamIntervalMs);
    view_->setBestPlaybackEnabled(settings.bestPlaybackEnabled);
    view_->setBestPlaybackIntervalMs(settings.bestPlaybackIntervalMs);

    const auto result = sendTrainingStreamConfig(
        sm,
        settings.streamIntervalMs,
        settings.bestPlaybackEnabled,
        settings.bestPlaybackIntervalMs);
    if (result.isError()) {
        LOG_WARN(
            State,
            "TrainingStreamConfigSet failed (intervalMs={}, bestPlaybackEnabled={}, "
            "bestPlaybackIntervalMs={}): {}",
            settings.streamIntervalMs,
            settings.bestPlaybackEnabled,
            settings.bestPlaybackIntervalMs,
            result.errorValue());
        return std::move(*this);
    }

    LOG_INFO(
        State,
        "Training stream config set (interval={}ms, bestPlaybackEnabled={}, "
        "bestPlaybackInterval={}ms)",
        result.value().intervalMs,
        result.value().bestPlaybackEnabled,
        result.value().bestPlaybackIntervalMs);
    return std::move(*this);
}

State::Any TrainingActive::onEvent(const UiApi::TrainingQuit::Cwc& cwc, StateMachine& sm)
{
    auto nextState = onEvent(QuitTrainingClickedEvent{}, sm);
    cwc.sendResponse(UiApi::TrainingQuit::Response::okay({ .queued = true }));
    return nextState;
}

State::Any TrainingActive::onEvent(const UiUpdateEvent& evt, StateMachine& /*sm*/)
{
    DIRTSIM_ASSERT(view_, "TrainingActiveView must exist");
    const auto now = std::chrono::steady_clock::now();
    if (lastRenderRateLog_ == std::chrono::steady_clock::time_point{}) {
        lastRenderRateLog_ = now;
        renderMessageCount_ = 0;
    }

    renderMessageCount_++;
    const auto elapsed = now - lastRenderRateLog_;
    if (elapsed >= std::chrono::seconds(1)) {
        const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
        const double rate = elapsedSeconds > 0.0 ? (renderMessageCount_ / elapsedSeconds) : 0.0;
        LOG_INFO(State, "Training render msg rate: {:.1f} msgs/s", rate);
        renderMessageCount_ = 0;
        lastRenderRateLog_ = now;
    }

    view_->renderWorld(evt.worldData);
    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
