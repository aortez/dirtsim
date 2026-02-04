#include "TrainingActive.h"
#include "StartMenu.h"
#include "State.h"
#include "TrainingUnsavedResult.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/EvolutionStop.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/TrainingStreamConfigSet.h"
#include "ui/TrainingActiveView.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include <algorithm>
#include <atomic>
#include <utility>

namespace DirtSim {
namespace Ui {
namespace State {
namespace {

Result<Api::TrainingStreamConfigSet::OkayType, std::string> sendTrainingStreamConfig(
    StateMachine& sm, int intervalMs)
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
    };
    const auto result =
        wsService.sendCommandAndGetResponse<Api::TrainingStreamConfigSet::OkayType>(cmd, 2000);
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
    state.trainingPaused_ = false;
    state.progressEventCount_ = 0;
    state.renderMessageCount_ = 0;
    state.lastRenderRateLog_ = std::chrono::steady_clock::now();
    state.uiLoopCount_ = 0;
    state.lastUiLoopLog_ = std::chrono::steady_clock::now();
    state.lastProgressRateLog_ = std::chrono::steady_clock::now();

    if (state.view_) {
        state.view_->setEvolutionStarted(true);
        state.view_->setTrainingPaused(false);
        state.view_->clearPanelContent();
        state.view_->createCorePanel();
    }

    if (!sm.hasWebSocketService()) {
        LOG_WARN(State, "No WebSocketService available for training stream setup");
        return;
    }

    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, cannot setup training streams");
        return;
    }

    const auto streamResult = sendTrainingStreamConfig(sm, sm.getUserSettings().streamIntervalMs);
    if (streamResult.isError()) {
        LOG_WARN(
            State,
            "TrainingStreamConfigSet failed (intervalMs={}): {}",
            sm.getUserSettings().streamIntervalMs,
            streamResult.errorValue());
    }
    else {
        LOG_INFO(State, "Training stream interval set to {}ms", streamResult.value().intervalMs);
    }

    static std::atomic<uint64_t> nextId{ 1 };
    Api::RenderFormatSet::Command renderCmd;
    renderCmd.format = RenderFormat::EnumType::Basic;

    auto envelope = DirtSim::Network::make_command_envelope(nextId.fetch_add(1), renderCmd);
    auto renderResult = wsService.sendBinaryAndReceive(envelope);
    if (renderResult.isError()) {
        LOG_ERROR(State, "Failed to subscribe to render stream: {}", renderResult.errorValue());
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

TrainingActive::TrainingActive(TrainingSpec lastTrainingSpec, bool hasTrainingSpec)
    : lastTrainingSpec_(std::move(lastTrainingSpec)), hasTrainingSpec_(hasTrainingSpec)
{}

TrainingActive::~TrainingActive() = default;

void TrainingActive::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Training active state");

    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        LOG_ERROR(State, "No UiComponentManager available");
        return;
    }

    DirtSim::Network::WebSocketServiceInterface* wsService = nullptr;
    if (sm.hasWebSocketService()) {
        wsService = &sm.getWebSocketService();
    }

    view_ = std::make_unique<TrainingActiveView>(uiManager, sm, wsService, sm.getUserSettings());

    if (auto* iconRail = uiManager->getIconRail()) {
        if (lv_obj_t* railContainer = iconRail->getContainer()) {
            lv_obj_add_flag(railContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(railContainer, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
    }
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

    if (view_) {
        view_->updateAnimations();
    }
}

bool TrainingActive::isTrainingResultModalVisible() const
{
    return view_ && view_->isTrainingResultModalVisible();
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
    if (elapsed >= std::chrono::seconds(1)) {
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

    if (view_) {
        view_->updateProgress(progress);
    }

    return std::move(*this);
}

State::Any TrainingActive::onEvent(
    const TrainingBestSnapshotReceivedEvent& evt, StateMachine& /*sm*/)
{
    if (!view_) {
        return std::move(*this);
    }

    WorldData worldData = evt.snapshot.worldData;
    worldData.organism_ids = evt.snapshot.organismIds;
    LOG_INFO(
        State,
        "Training best snapshot received: fitness={:.4f} gen={} world={}x{} cells={} colors={} "
        "organism_ids={}",
        evt.snapshot.fitness,
        evt.snapshot.generation,
        worldData.width,
        worldData.height,
        worldData.cells.size(),
        worldData.colors.size(),
        worldData.organism_ids.size());
    view_->updateBestSnapshot(worldData, evt.snapshot.fitness, evt.snapshot.generation);

    return std::move(*this);
}

State::Any TrainingActive::onEvent(const Api::TrainingResult::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Training result available (candidates={})", cwc.command.candidates.size());

    trainingPaused_ = false;
    const GenomeId bestGenomeId = getBestGenomeId(cwc.command.candidates);

    if (view_) {
        view_->setEvolutionCompleted(bestGenomeId);
        view_->setTrainingPaused(false);
    }

    cwc.sendResponse(Api::TrainingResult::Response::okay(std::monostate{}));

    return TrainingUnsavedResult{
        lastTrainingSpec_, hasTrainingSpec_, cwc.command.summary, cwc.command.candidates
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
    if (view_) {
        view_->setTrainingPaused(trainingPaused_);
    }

    LOG_INFO(State, "Training pause toggled: {}", trainingPaused_);
    return std::move(*this);
}

State::Any TrainingActive::onEvent(const TrainingStreamConfigChangedEvent& evt, StateMachine& sm)
{
    auto& settings = sm.getUserSettings();
    settings.streamIntervalMs = std::max(0, evt.intervalMs);

    if (view_) {
        view_->setStreamIntervalMs(settings.streamIntervalMs);
    }

    const auto result = sendTrainingStreamConfig(sm, settings.streamIntervalMs);
    if (result.isError()) {
        LOG_WARN(
            State,
            "TrainingStreamConfigSet failed (intervalMs={}): {}",
            settings.streamIntervalMs,
            result.errorValue());
        return std::move(*this);
    }

    LOG_INFO(State, "Training stream interval set to {}ms", result.value().intervalMs);
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
    if (view_) {
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
    }

    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
