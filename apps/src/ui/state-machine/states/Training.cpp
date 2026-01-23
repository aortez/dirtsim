#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/SeedAdd.h"
#include "server/api/SimRun.h"
#include "server/api/TrainingResultDiscard.h"
#include "server/api/TrainingResultSave.h"
#include "ui/RemoteInputDevice.h"
#include "ui/TrainingView.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include <algorithm>
#include <atomic>

namespace DirtSim {
namespace Ui {
namespace State {

void Training::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Training state (waiting for start command)");
    evolutionStarted_ = false;

    // Create training view.
    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        LOG_ERROR(State, "No UiComponentManager available");
        return;
    }

    Network::WebSocketServiceInterface* wsService = nullptr;
    if (sm.hasWebSocketService()) {
        wsService = &sm.getWebSocketService();
    }
    view_ = std::make_unique<TrainingView>(uiManager, sm, wsService);

    IconRail* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setVisibleIcons(
        { IconId::CORE, IconId::EVOLUTION, IconId::GENOME_BROWSER, IconId::TRAINING_RESULTS });
    iconRail->deselectAll(); // Start fresh, no panel open.
}

void Training::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting Training state");

    // Clear panel content before view is destroyed.
    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->hide();
        }
    }

    view_.reset();
}

void Training::updateAnimations()
{
    if (view_) {
        view_->updateAnimations();
    }
}

bool Training::isTrainingResultModalVisible() const
{
    return view_ && view_->isTrainingResultModalVisible();
}

State::Any Training::onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& /*sm*/)
{
    // Update progress from server broadcast.
    progress = evt.progress;

    LOG_DEBUG(
        State,
        "Evolution progress: gen {}/{}, eval {}/{}, best fitness {:.2f}",
        progress.generation,
        progress.maxGenerations,
        progress.currentEval,
        progress.populationSize,
        progress.bestFitnessAllTime);

    // Detect training completion.
    const bool isComplete =
        (progress.maxGenerations > 0 && progress.generation >= progress.maxGenerations
         && progress.currentEval >= progress.populationSize);

    if (isComplete) {
        LOG_INFO(State, "Evolution completed, ready for new run");
        evolutionStarted_ = false;
    }

    // Update UI progress bars and labels.
    if (view_) {
        view_->updateProgress(progress);
    }

    // Stay in Training state.
    return std::move(*this);
}

State::Any Training::onEvent(const Api::TrainingResult::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Training result available (candidates={})", cwc.command.candidates.size());

    if (view_) {
        view_->showTrainingResultModal(cwc.command.summary, cwc.command.candidates);
    }

    cwc.sendResponse(Api::TrainingResult::Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any Training::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "Icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    auto* uiManager = sm.getUiComponentManager();
    auto* panel = uiManager->getExpandablePanel();

    if (!panel || !view_) {
        return std::move(*this);
    }

    // Closing panel (deselected icon).
    if (evt.selectedId == IconId::COUNT) {
        view_->clearPanelContent();
        panel->clearContent();
        panel->hide();
        return std::move(*this);
    }

    // Opening or switching panel.
    view_->clearPanelContent();
    panel->clearContent();

    switch (evt.selectedId) {
        case IconId::CORE:
            view_->createCorePanel();
            panel->show();
            break;

        case IconId::EVOLUTION:
            view_->createTrainingConfigPanel();
            panel->show();
            break;

        case IconId::GENOME_BROWSER:
            view_->createGenomeBrowserPanel();
            panel->show();
            break;

        case IconId::TRAINING_RESULTS:
            view_->createTrainingResultBrowserPanel();
            panel->show();
            break;

        case IconId::TREE:
        case IconId::NETWORK:
        case IconId::PHYSICS:
        case IconId::PLAY:
        case IconId::SCENARIO:
        case IconId::COUNT:
            DIRTSIM_ASSERT(false, "Unexpected icon selection in Training state");
            break;
    }

    return std::move(*this);
}

State::Any Training::onEvent(const RailAutoShrinkRequestEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Auto-shrink requested, minimizing IconRail");

    // Process auto-shrink in main thread (safe to modify LVGL objects).
    if (auto* iconRail = sm.getUiComponentManager()->getIconRail()) {
        iconRail->setMode(RailMode::Minimized);
    }

    return std::move(*this);
}

State::Any Training::onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm)
{
    LOG_WARN(State, "Server disconnected during training (reason: {})", evt.reason);
    LOG_INFO(State, "Transitioning to Disconnected");

    if (!sm.queueReconnectToLastServer()) {
        LOG_WARN(State, "No previous server address available for reconnect");
    }

    // Lost connection - go back to Disconnected state.
    return Disconnected{};
}

State::Any Training::onEvent(const StartEvolutionButtonClickedEvent& evt, StateMachine& sm)
{
    if (evolutionStarted_) {
        LOG_WARN(State, "Evolution already started, ignoring duplicate start");
        return std::move(*this);
    }

    LOG_INFO(
        State,
        "Starting evolution: population={}, generations={}, scenario={}, organism_type={}",
        evt.evolution.populationSize,
        evt.evolution.maxGenerations,
        toString(evt.training.scenarioId),
        static_cast<int>(evt.training.organismType));

    if (!sm.hasWebSocketService()) {
        LOG_ERROR(State, "No WebSocketService available");
        return std::move(*this);
    }

    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, cannot start evolution");
        return std::move(*this);
    }

    // Send EvolutionStart to server with provided config.
    Api::EvolutionStart::Command cmd;
    cmd.evolution = evt.evolution;
    cmd.mutation = evt.mutation;
    cmd.scenarioId = evt.training.scenarioId;
    cmd.organismType = evt.training.organismType;
    cmd.population = evt.training.population;

    const auto result =
        wsService.sendCommandAndGetResponse<Api::EvolutionStart::OkayType>(cmd, 5000);
    if (result.isError()) {
        LOG_ERROR(State, "Failed to send EvolutionStart: {}", result.errorValue());
        return std::move(*this);
    }
    if (result.value().isError()) {
        LOG_ERROR(State, "Server EvolutionStart error: {}", result.value().errorValue().message);
        return std::move(*this);
    }

    LOG_INFO(State, "Evolution started on server");
    evolutionStarted_ = true;
    lastTrainingSpec_ = evt.training;
    hasTrainingSpec_ = true;

    // Subscribe to render stream for live training view.
    static std::atomic<uint64_t> nextId{ 1 };
    Api::RenderFormatSet::Command renderCmd;
    renderCmd.format = RenderFormat::EnumType::Basic;

    auto envelope = Network::make_command_envelope(nextId.fetch_add(1), renderCmd);
    auto renderResult = wsService.sendBinaryAndReceive(envelope);
    if (renderResult.isError()) {
        LOG_ERROR(State, "Failed to subscribe to render stream: {}", renderResult.errorValue());
    }
    else {
        LOG_INFO(State, "Subscribed to render stream for live training view");
    }

    // Update UI to show "running" state.
    if (view_) {
        view_->setEvolutionStarted(true);
        view_->hideTrainingResultModal();
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

    if (view_) {
        view_->clearPanelContent();
    }

    return std::move(*this);
}

State::Any Training::onEvent(const UiApi::TrainingStart::Cwc& cwc, StateMachine& sm)
{
    StartEvolutionButtonClickedEvent evt{
        .evolution = cwc.command.evolution,
        .mutation = cwc.command.mutation,
        .training = cwc.command.training,
    };
    auto nextState = onEvent(evt, sm);
    cwc.sendResponse(UiApi::TrainingStart::Response::okay({ .queued = true }));
    return nextState;
}

State::Any Training::onEvent(const UiApi::TrainingResultSave::Cwc& cwc, StateMachine& sm)
{
    if (!view_ || !view_->isTrainingResultModalVisible()) {
        cwc.sendResponse(
            UiApi::TrainingResultSave::Response::error(
                ApiError("Training result modal not visible")));
        return std::move(*this);
    }

    std::vector<GenomeId> ids;
    if (cwc.command.count.has_value()) {
        ids = view_->getTrainingResultSaveIdsForCount(cwc.command.count.value());
    }
    else {
        ids = view_->getTrainingResultSaveIds();
    }
    if (ids.empty()) {
        cwc.sendResponse(
            UiApi::TrainingResultSave::Response::error(ApiError("No candidates selected")));
        return std::move(*this);
    }

    TrainingResultSaveClickedEvent evt{ .ids = std::move(ids) };
    auto nextState = onEvent(evt, sm);
    cwc.sendResponse(UiApi::TrainingResultSave::Response::okay({ .queued = true }));
    return nextState;
}

State::Any Training::onEvent(const UiApi::TrainingResultDiscard::Cwc& cwc, StateMachine& sm)
{
    if (!view_ || !view_->isTrainingResultModalVisible()) {
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(
                ApiError("Training result modal not visible")));
        return std::move(*this);
    }

    auto nextState = onEvent(TrainingResultDiscardClickedEvent{}, sm);
    cwc.sendResponse(UiApi::TrainingResultDiscard::Response::okay({ .queued = true }));
    return nextState;
}

State::Any Training::onEvent(const StopTrainingClickedEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Stop button clicked, stopping evolution");

    DIRTSIM_ASSERT(sm.hasWebSocketService(), "WebSocketService must exist");
    auto& wsService = sm.getWebSocketService();
    DIRTSIM_ASSERT(wsService.isConnected(), "Must be connected to reach Training state");

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

State::Any Training::onEvent(const QuitTrainingClickedEvent& /*evt*/, StateMachine& sm)
{
    if (!evolutionStarted_) {
        LOG_INFO(State, "Quit button clicked, returning to start menu");
        return StartMenu{};
    }

    LOG_INFO(State, "Quit button clicked, stopping evolution");

    DIRTSIM_ASSERT(sm.hasWebSocketService(), "WebSocketService must exist");
    auto& wsService = sm.getWebSocketService();
    DIRTSIM_ASSERT(wsService.isConnected(), "Must be connected to reach Training state");

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

State::Any Training::onEvent(const TrainingResultSaveClickedEvent& evt, StateMachine& sm)
{
    LOG_INFO(State, "Training result save requested (count={})", evt.ids.size());

    if (evt.ids.empty()) {
        LOG_WARN(State, "Training result save ignored: no ids provided");
        return std::move(*this);
    }

    if (!sm.hasWebSocketService()) {
        LOG_ERROR(State, "No WebSocketService available");
        return std::move(*this);
    }
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, cannot save training result");
        return std::move(*this);
    }

    Api::TrainingResultSave::Command cmd;
    cmd.ids = evt.ids;
    const auto result =
        wsService.sendCommandAndGetResponse<Api::TrainingResultSave::OkayType>(cmd, 5000);
    if (result.isError()) {
        LOG_ERROR(State, "TrainingResultSave failed: {}", result.errorValue());
        return std::move(*this);
    }
    if (result.value().isError()) {
        LOG_ERROR(State, "TrainingResultSave error: {}", result.value().errorValue().message);
        return std::move(*this);
    }

    if (view_) {
        view_->hideTrainingResultModal();
    }

    return std::move(*this);
}

State::Any Training::onEvent(const TrainingResultDiscardClickedEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Training result discard requested");

    if (!sm.hasWebSocketService()) {
        LOG_ERROR(State, "No WebSocketService available");
        return std::move(*this);
    }
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, cannot discard training result");
        return std::move(*this);
    }

    Api::TrainingResultDiscard::Command cmd;
    const auto result =
        wsService.sendCommandAndGetResponse<Api::TrainingResultDiscard::OkayType>(cmd, 5000);
    if (result.isError()) {
        LOG_ERROR(State, "TrainingResultDiscard failed: {}", result.errorValue());
        return std::move(*this);
    }
    if (result.value().isError()) {
        LOG_ERROR(State, "TrainingResultDiscard error: {}", result.value().errorValue().message);
        return std::move(*this);
    }

    if (view_) {
        view_->hideTrainingResultModal();
    }

    return std::move(*this);
}

State::Any Training::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state.
    return Shutdown{};
}

State::Any Training::onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm)
{
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(true);
    }

    cwc.sendResponse(UiApi::MouseDown::Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any Training::onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm)
{
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
    }

    cwc.sendResponse(UiApi::MouseMove::Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any Training::onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm)
{
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(false);
    }

    cwc.sendResponse(UiApi::MouseUp::Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any Training::onEvent(const UiUpdateEvent& evt, StateMachine& /*sm*/)
{
    // Render live training world.
    if (view_ && evolutionStarted_) {
        view_->renderWorld(evt.worldData);
    }

    return std::move(*this);
}

State::Any Training::onEvent(const ViewBestButtonClickedEvent& evt, StateMachine& sm)
{
    LOG_INFO(State, "View Best clicked, genome_id={}", evt.genomeId.toShortString());

    if (!hasTrainingSpec_) {
        LOG_WARN(State, "View Best ignored: no training spec available");
        return std::move(*this);
    }
    if (lastTrainingSpec_.organismType != OrganismType::TREE) {
        LOG_WARN(State, "View Best only supported for tree training");
        return std::move(*this);
    }
    if (evt.genomeId.isNil()) {
        LOG_WARN(State, "View Best ignored: genome_id is nil");
        return std::move(*this);
    }

    DIRTSIM_ASSERT(sm.hasWebSocketService(), "WebSocketService must exist");
    auto& wsService = sm.getWebSocketService();
    DIRTSIM_ASSERT(wsService.isConnected(), "Must be connected");

    // Stop evolution if running.
    if (evolutionStarted_) {
        Api::EvolutionStop::Command stopCmd;
        auto stopResult =
            wsService.sendCommandAndGetResponse<Api::EvolutionStop::OkayType>(stopCmd, 2000);
        DIRTSIM_ASSERT(
            stopResult.isValue() && stopResult.value().isValue(), "EvolutionStop failed");
        evolutionStarted_ = false;
    }

    lv_disp_t* disp = lv_disp_get_default();
    int16_t dispWidth = static_cast<int16_t>(lv_disp_get_hor_res(disp));
    int16_t dispHeight = static_cast<int16_t>(lv_disp_get_ver_res(disp));
    Vector2s containerSize{ static_cast<int16_t>(dispWidth - IconRail::MINIMIZED_RAIL_WIDTH),
                            dispHeight };

    Api::SimRun::Command simRunCmd{ .timestep = 0.016,
                                    .max_steps = -1,
                                    .max_frame_ms = 16,
                                    .scenario_id = lastTrainingSpec_.scenarioId,
                                    .start_paused = false,
                                    .container_size = containerSize };

    auto simResult = wsService.sendCommandAndGetResponse<Api::SimRun::Okay>(simRunCmd, 2000);
    if (simResult.isError() || simResult.value().isError()) {
        LOG_ERROR(State, "SimRun failed");
        return std::move(*this);
    }

    constexpr int targetCellSize = 16;
    const int worldWidth = std::max(10, static_cast<int>(containerSize.x) / targetCellSize);
    const int worldHeight = std::max(10, static_cast<int>(containerSize.y) / targetCellSize);
    const int centerX = worldWidth / 2;
    const int centerY = worldHeight / 2;

    Api::SeedAdd::Command seedCmd;
    seedCmd.x = centerX;
    seedCmd.y = centerY;
    seedCmd.genome_id = evt.genomeId.toString();

    auto seedResult = wsService.sendCommandAndGetResponse<Api::SeedAdd::OkayType>(seedCmd, 2000);
    if (seedResult.isError() || seedResult.value().isError()) {
        LOG_ERROR(State, "SeedAdd failed");
    }

    LOG_INFO(State, "Transitioning to SimRunning with best genome");
    return SimRunning{};
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
