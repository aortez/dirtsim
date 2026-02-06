#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "server/api/GenomeGet.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/SeedAdd.h"
#include "server/api/SimRun.h"
#include "server/api/TrainingResultDiscard.h"
#include "server/api/TrainingResultSave.h"
#include "server/api/TrainingStreamConfigSet.h"
#include "ui/RemoteInputDevice.h"
#include "ui/TrainingActiveView.h"
#include "ui/TrainingIdleView.h"
#include "ui/TrainingUnsavedResultView.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include <algorithm>
#include <atomic>

namespace DirtSim {
namespace Ui {
namespace State {

namespace {

Result<Api::TrainingResultSave::OkayType, std::string> saveTrainingResultToServer(
    StateMachine& sm, const std::vector<GenomeId>& ids, bool restart)
{
    if (ids.empty()) {
        return Result<Api::TrainingResultSave::OkayType, std::string>::error("No ids provided");
    }
    if (!sm.hasWebSocketService()) {
        return Result<Api::TrainingResultSave::OkayType, std::string>::error(
            "No WebSocketService available");
    }

    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        return Result<Api::TrainingResultSave::OkayType, std::string>::error(
            "Not connected to server");
    }

    Api::TrainingResultSave::Command cmd;
    cmd.ids = ids;
    cmd.restart = restart;
    const auto result =
        wsService.sendCommandAndGetResponse<Api::TrainingResultSave::OkayType>(cmd, 5000);
    if (result.isError()) {
        return Result<Api::TrainingResultSave::OkayType, std::string>::error(result.errorValue());
    }
    if (result.value().isError()) {
        return Result<Api::TrainingResultSave::OkayType, std::string>::error(
            result.value().errorValue().message);
    }

    return Result<Api::TrainingResultSave::OkayType, std::string>::okay(result.value().value());
}

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

State::Any handleExitCommand(const UiApi::Exit::Cwc& cwc)
{
    LOG_INFO(State, "Exit command received, shutting down");

    cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));

    return Shutdown{};
}

void handleRemoteMouseDown(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm)
{
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(true);
    }

    cwc.sendResponse(UiApi::MouseDown::Response::okay(std::monostate{}));
}

void handleRemoteMouseMove(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm)
{
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
    }

    cwc.sendResponse(UiApi::MouseMove::Response::okay(std::monostate{}));
}

void handleRemoteMouseUp(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm)
{
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(false);
    }

    cwc.sendResponse(UiApi::MouseUp::Response::okay(std::monostate{}));
}

} // namespace

// =============================
// TrainingIdle
// =============================

TrainingIdle::TrainingIdle(TrainingSpec lastTrainingSpec, bool hasTrainingSpec)
    : lastTrainingSpec_(std::move(lastTrainingSpec)), hasTrainingSpec_(hasTrainingSpec)
{}

TrainingIdle::~TrainingIdle() = default;

void TrainingIdle::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Training idle state (waiting for start command)");

    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        LOG_ERROR(State, "No UiComponentManager available");
        return;
    }

    DirtSim::Network::WebSocketServiceInterface* wsService = nullptr;
    if (sm.hasWebSocketService()) {
        wsService = &sm.getWebSocketService();
    }

    view_ = std::make_unique<TrainingIdleView>(uiManager, sm, wsService, sm.getUserSettings());

    if (auto* panel = uiManager->getExpandablePanel()) {
        panel->clearContent();
        panel->hide();
        panel->resetWidth();
    }

    IconRail* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    if (lv_obj_t* railContainer = iconRail->getContainer()) {
        lv_obj_clear_flag(railContainer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(railContainer, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
    iconRail->setLayout(RailLayout::SingleColumn);
    iconRail->setVisibleIcons(
        { IconId::DUCK, IconId::EVOLUTION, IconId::GENOME_BROWSER, IconId::TRAINING_RESULTS });
    iconRail->deselectAll();

    if (view_) {
        view_->setEvolutionStarted(false);
        view_->clearPanelContent();
        view_->updateIconRailOffset();
    }
}

void TrainingIdle::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting Training idle state");

    view_.reset();

    // Clear panel content after view cleanup.
    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->hide();
        }
    }
}

void TrainingIdle::updateAnimations()
{
    if (view_) {
        view_->updateAnimations();
    }
}

bool TrainingIdle::isTrainingResultModalVisible() const
{
    return view_ && view_->isTrainingResultModalVisible();
}

State::Any TrainingIdle::onEvent(
    const EvolutionProgressReceivedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(
    const TrainingBestSnapshotReceivedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "Icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    if (!view_) {
        return std::move(*this);
    }

    if (evt.selectedId == IconId::DUCK) {
        LOG_INFO(State, "Start menu icon selected, returning to start menu");
        if (auto* iconRail = sm.getUiComponentManager()->getIconRail()) {
            iconRail->deselectAll();
        }
        return StartMenu{};
    }

    // Closing panel (deselected icon).
    if (evt.selectedId == IconId::NONE) {
        view_->clearPanelContent();
        return std::move(*this);
    }

    view_->clearPanelContent();

    switch (evt.selectedId) {
        case IconId::EVOLUTION:
            view_->createTrainingConfigPanel();
            break;

        case IconId::GENOME_BROWSER:
            view_->createGenomeBrowserPanel();
            break;

        case IconId::TRAINING_RESULTS:
            view_->createTrainingResultBrowserPanel();
            break;

        case IconId::TREE:
        case IconId::MUSIC:
        case IconId::NETWORK:
        case IconId::DUCK:
        case IconId::CORE:
        case IconId::PHYSICS:
        case IconId::PLAY:
        case IconId::SCENARIO:
        case IconId::SETTINGS:
        case IconId::NONE:
            DIRTSIM_ASSERT(false, "Unexpected icon selection in Training idle");
            break;
    }

    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const RailAutoShrinkRequestEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Auto-shrink requested, minimizing IconRail");

    if (auto* iconRail = sm.getUiComponentManager()->getIconRail()) {
        iconRail->setMode(RailMode::Minimized);
    }

    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm)
{
    LOG_WARN(State, "Server disconnected during training (reason: {})", evt.reason);
    LOG_INFO(State, "Transitioning to Disconnected");

    if (!sm.queueReconnectToLastServer()) {
        LOG_WARN(State, "No previous server address available for reconnect");
    }

    return Disconnected{};
}

State::Any TrainingIdle::onEvent(const StartEvolutionButtonClickedEvent& evt, StateMachine& sm)
{
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
    lastTrainingSpec_ = evt.training;
    hasTrainingSpec_ = true;

    return TrainingActive{ lastTrainingSpec_, hasTrainingSpec_ };
}

State::Any TrainingIdle::onEvent(const UiApi::TrainingStart::Cwc& cwc, StateMachine& sm)
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

State::Any TrainingIdle::onEvent(const UiApi::TrainingResultSave::Cwc& cwc, StateMachine& /*sm*/)
{
    cwc.sendResponse(
        UiApi::TrainingResultSave::Response::error(ApiError("Training result modal not visible")));
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const UiApi::GenomeBrowserOpen::Cwc& cwc, StateMachine& sm)
{
    using Response = UiApi::GenomeBrowserOpen::Response;

    if (!view_) {
        cwc.sendResponse(Response::error(ApiError("Training view not available")));
        return std::move(*this);
    }

    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        cwc.sendResponse(Response::error(ApiError("UiComponentManager not available")));
        return std::move(*this);
    }

    view_->clearPanelContent();
    view_->createGenomeBrowserPanel();

    if (auto* iconRail = uiManager->getIconRail()) {
        iconRail->selectIcon(IconId::GENOME_BROWSER);
    }

    cwc.sendResponse(Response::okay({ .opened = true }));
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const UiApi::GenomeDetailOpen::Cwc& cwc, StateMachine& /*sm*/)
{
    using Response = UiApi::GenomeDetailOpen::Response;

    if (!view_) {
        cwc.sendResponse(Response::error(ApiError("Training view not available")));
        return std::move(*this);
    }

    Result<GenomeId, std::string> result;
    if (cwc.command.id.has_value()) {
        result = view_->openGenomeDetailById(cwc.command.id.value());
    }
    else {
        result = view_->openGenomeDetailByIndex(cwc.command.index);
    }
    if (result.isError()) {
        cwc.sendResponse(Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    UiApi::GenomeDetailOpen::Okay response{
        .opened = true,
        .id = result.value(),
    };
    cwc.sendResponse(Response::okay(std::move(response)));
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const UiApi::GenomeDetailLoad::Cwc& cwc, StateMachine& /*sm*/)
{
    using Response = UiApi::GenomeDetailLoad::Response;

    if (!view_) {
        cwc.sendResponse(Response::error(ApiError("Training view not available")));
        return std::move(*this);
    }

    auto result = view_->loadGenomeDetail(cwc.command.id);
    if (result.isError()) {
        cwc.sendResponse(Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(Response::okay({ .queued = true }));
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(
    const UiApi::TrainingConfigShowEvolution::Cwc& cwc, StateMachine& /*sm*/)
{
    using Response = UiApi::TrainingConfigShowEvolution::Response;

    if (!view_) {
        cwc.sendResponse(Response::error(ApiError("Training view not available")));
        return std::move(*this);
    }

    auto result = view_->showTrainingConfigView(TrainingIdleView::TrainingConfigView::Evolution);
    if (result.isError()) {
        cwc.sendResponse(Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(Response::okay({ .opened = true }));
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const UiApi::TrainingQuit::Cwc& cwc, StateMachine& sm)
{
    auto nextState = onEvent(QuitTrainingClickedEvent{}, sm);
    cwc.sendResponse(UiApi::TrainingQuit::Response::okay({ .queued = true }));
    return nextState;
}

State::Any TrainingIdle::onEvent(const UiApi::TrainingResultDiscard::Cwc& cwc, StateMachine& /*sm*/)
{
    cwc.sendResponse(
        UiApi::TrainingResultDiscard::Response::error(
            ApiError("Training result modal not visible")));
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const TrainingStreamConfigChangedEvent& evt, StateMachine& sm)
{
    auto& settings = sm.getUserSettings();
    settings.streamIntervalMs = std::max(0, evt.intervalMs);

    if (view_) {
        view_->setStreamIntervalMs(settings.streamIntervalMs);
    }

    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const StopTrainingClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Stop button clicked while idle, returning to start menu");
    return StartMenu{};
}

State::Any TrainingIdle::onEvent(const QuitTrainingClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Quit button clicked, returning to start menu");
    return StartMenu{};
}

State::Any TrainingIdle::onEvent(const GenomeLoadClickedEvent& evt, StateMachine& sm)
{
    LOG_INFO(State, "Genome load requested (genome_id={})", evt.genomeId.toShortString());

    if (!sm.hasWebSocketService()) {
        LOG_WARN(State, "Genome load ignored: no WebSocketService");
        return std::move(*this);
    }

    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Genome load ignored: not connected to server");
        return std::move(*this);
    }

    Api::GenomeGet::Command getCmd{ .id = evt.genomeId };
    auto getResult = wsService.sendCommandAndGetResponse<Api::GenomeGet::Okay>(getCmd, 5000);
    if (getResult.isError()) {
        LOG_ERROR(State, "GenomeGet failed: {}", getResult.errorValue());
        return std::move(*this);
    }
    if (getResult.value().isError()) {
        LOG_ERROR(State, "GenomeGet error: {}", getResult.value().errorValue().message);
        return std::move(*this);
    }

    const auto& response = getResult.value().value();
    if (!response.found) {
        LOG_WARN(State, "Genome load ignored: genome not found");
        return std::move(*this);
    }
    if (!response.metadata.organismType.has_value()) {
        LOG_WARN(State, "Genome load ignored: missing organism type");
        return std::move(*this);
    }
    if (response.metadata.organismType.value() != OrganismType::TREE) {
        LOG_WARN(State, "Genome load only supported for tree organisms");
        return std::move(*this);
    }

    if (evt.genomeId.isNil()) {
        LOG_WARN(State, "Genome load ignored: genome_id is nil");
        return std::move(*this);
    }

    lv_disp_t* disp = lv_disp_get_default();
    int16_t dispWidth = static_cast<int16_t>(lv_disp_get_hor_res(disp));
    int16_t dispHeight = static_cast<int16_t>(lv_disp_get_ver_res(disp));
    Vector2s containerSize{ static_cast<int16_t>(dispWidth - IconRail::MINIMIZED_RAIL_WIDTH),
                            dispHeight };

    Api::SimRun::Command simRunCmd{ .timestep = 0.016,
                                    .max_steps = -1,
                                    .max_frame_ms = 16,
                                    .scenario_id = evt.scenarioId,
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

    LOG_INFO(State, "Transitioning to SimRunning with genome");
    return SimRunning{};
}

State::Any TrainingIdle::onEvent(
    const OpenTrainingGenomeBrowserClickedEvent& /*evt*/, StateMachine& sm)
{
    if (!view_) {
        LOG_WARN(State, "Training view not available for genome browser");
        return std::move(*this);
    }

    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        LOG_WARN(State, "UiComponentManager not available for genome browser");
        return std::move(*this);
    }

    view_->clearPanelContent();
    view_->createGenomeBrowserPanel();

    if (auto* iconRail = uiManager->getIconRail()) {
        iconRail->selectIcon(IconId::GENOME_BROWSER);
    }

    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const GenomeAddToTrainingClickedEvent& evt, StateMachine& /*sm*/)
{
    if (!view_) {
        LOG_WARN(State, "Training view not available for genome add");
        return std::move(*this);
    }

    view_->addGenomeToTraining(evt.genomeId, evt.scenarioId);
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    return handleExitCommand(cwc);
}

State::Any TrainingIdle::onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm)
{
    handleRemoteMouseDown(cwc, sm);
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm)
{
    handleRemoteMouseMove(cwc, sm);
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm)
{
    handleRemoteMouseUp(cwc, sm);
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const UiUpdateEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const ViewBestButtonClickedEvent& evt, StateMachine& sm)
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

// =============================
// TrainingActive
// =============================

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

State::Any TrainingActive::onEvent(const RailAutoShrinkRequestEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any TrainingActive::onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm)
{
    LOG_WARN(State, "Server disconnected during training (reason: {})", evt.reason);
    LOG_INFO(State, "Transitioning to Disconnected");

    if (!sm.queueReconnectToLastServer()) {
        LOG_WARN(State, "No previous server address available for reconnect");
    }

    return Disconnected{};
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

State::Any TrainingActive::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    return handleExitCommand(cwc);
}

State::Any TrainingActive::onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm)
{
    handleRemoteMouseDown(cwc, sm);
    return std::move(*this);
}

State::Any TrainingActive::onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm)
{
    handleRemoteMouseMove(cwc, sm);
    return std::move(*this);
}

State::Any TrainingActive::onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm)
{
    handleRemoteMouseUp(cwc, sm);
    return std::move(*this);
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

// =============================
// TrainingUnsavedResult
// =============================

TrainingUnsavedResult::TrainingUnsavedResult(
    TrainingSpec lastTrainingSpec,
    bool hasTrainingSpec,
    Api::TrainingResult::Summary summary,
    std::vector<Api::TrainingResult::Candidate> candidates)
    : lastTrainingSpec_(std::move(lastTrainingSpec)),
      hasTrainingSpec_(hasTrainingSpec),
      summary_(std::move(summary)),
      candidates_(std::move(candidates))
{}

TrainingUnsavedResult::~TrainingUnsavedResult() = default;

void TrainingUnsavedResult::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Training unsaved-result state");

    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        LOG_ERROR(State, "No UiComponentManager available");
        return;
    }

    view_ = std::make_unique<TrainingUnsavedResultView>(uiManager, sm);

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

    if (view_) {
        view_->showTrainingResultModal(summary_, candidates_);
    }
}

void TrainingUnsavedResult::onExit(StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exiting Training unsaved-result state");
}

void TrainingUnsavedResult::updateAnimations()
{
    if (view_) {
        view_->updateAnimations();
    }
}

bool TrainingUnsavedResult::isTrainingResultModalVisible() const
{
    return view_ && view_->isTrainingResultModalVisible();
}

State::Any TrainingUnsavedResult::onEvent(
    const TrainingResultSaveClickedEvent& evt, StateMachine& sm)
{
    LOG_INFO(State, "Training result save requested (count={})", evt.ids.size());

    if (evt.ids.empty()) {
        LOG_WARN(State, "Training result save ignored: no ids provided");
        return std::move(*this);
    }

    const auto result = saveTrainingResultToServer(sm, evt.ids, evt.restart);
    if (result.isError()) {
        LOG_ERROR(State, "TrainingResultSave failed: {}", result.errorValue());
        return std::move(*this);
    }

    if (evt.restart) {
        return TrainingActive{ lastTrainingSpec_, hasTrainingSpec_ };
    }

    if (view_) {
        view_->hideTrainingResultModal();
    }

    return TrainingIdle{ lastTrainingSpec_, hasTrainingSpec_ };
}

State::Any TrainingUnsavedResult::onEvent(
    const TrainingResultDiscardClickedEvent& /*evt*/, StateMachine& sm)
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

    return TrainingIdle{ lastTrainingSpec_, hasTrainingSpec_ };
}

State::Any TrainingUnsavedResult::onEvent(
    const UiApi::TrainingResultSave::Cwc& cwc, StateMachine& sm)
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

    const bool restartRequested = cwc.command.restart;
    const auto saveResult = saveTrainingResultToServer(sm, ids, restartRequested);
    if (saveResult.isError()) {
        LOG_ERROR(State, "TrainingResultSave failed: {}", saveResult.errorValue());
        cwc.sendResponse(
            UiApi::TrainingResultSave::Response::error(ApiError(saveResult.errorValue())));
        return std::move(*this);
    }

    UiApi::TrainingResultSave::Okay response{
        .queued = false,
        .savedCount = saveResult.value().savedCount,
        .discardedCount = saveResult.value().discardedCount,
        .savedIds = saveResult.value().savedIds,
    };
    cwc.sendResponse(UiApi::TrainingResultSave::Response::okay(std::move(response)));

    if (restartRequested) {
        return TrainingActive{ lastTrainingSpec_, hasTrainingSpec_ };
    }

    if (view_) {
        view_->hideTrainingResultModal();
    }

    return TrainingIdle{ lastTrainingSpec_, hasTrainingSpec_ };
}

State::Any TrainingUnsavedResult::onEvent(
    const UiApi::TrainingResultDiscard::Cwc& cwc, StateMachine& sm)
{
    if (!view_ || !view_->isTrainingResultModalVisible()) {
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(
                ApiError("Training result modal not visible")));
        return std::move(*this);
    }

    if (!sm.hasWebSocketService()) {
        LOG_ERROR(State, "No WebSocketService available");
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(ApiError("No WebSocketService")));
        return std::move(*this);
    }
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, cannot discard training result");
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(ApiError("Not connected to server")));
        return std::move(*this);
    }

    Api::TrainingResultDiscard::Command cmd;
    const auto result =
        wsService.sendCommandAndGetResponse<Api::TrainingResultDiscard::OkayType>(cmd, 5000);
    if (result.isError()) {
        LOG_ERROR(State, "TrainingResultDiscard failed: {}", result.errorValue());
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }
    if (result.value().isError()) {
        LOG_ERROR(State, "TrainingResultDiscard error: {}", result.value().errorValue().message);
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(
                ApiError(result.value().errorValue().message)));
        return std::move(*this);
    }

    cwc.sendResponse(UiApi::TrainingResultDiscard::Response::okay({ .queued = true }));

    if (view_) {
        view_->hideTrainingResultModal();
    }

    return TrainingIdle{ lastTrainingSpec_, hasTrainingSpec_ };
}

State::Any TrainingUnsavedResult::onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm)
{
    LOG_WARN(State, "Server disconnected during training (reason: {})", evt.reason);
    LOG_INFO(State, "Transitioning to Disconnected");

    if (!sm.queueReconnectToLastServer()) {
        LOG_WARN(State, "No previous server address available for reconnect");
    }

    return Disconnected{};
}

State::Any TrainingUnsavedResult::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    return handleExitCommand(cwc);
}

State::Any TrainingUnsavedResult::onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm)
{
    handleRemoteMouseDown(cwc, sm);
    return std::move(*this);
}

State::Any TrainingUnsavedResult::onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm)
{
    handleRemoteMouseMove(cwc, sm);
    return std::move(*this);
}

State::Any TrainingUnsavedResult::onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm)
{
    handleRemoteMouseUp(cwc, sm);
    return std::move(*this);
}

State::Any TrainingUnsavedResult::onEvent(const UiUpdateEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any TrainingUnsavedResult::onEvent(
    const EvolutionProgressReceivedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any TrainingUnsavedResult::onEvent(
    const TrainingBestSnapshotReceivedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
