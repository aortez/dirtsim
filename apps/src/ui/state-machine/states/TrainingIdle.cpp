#include "TrainingIdle.h"
#include "SimRunning.h"
#include "StartMenu.h"
#include "State.h"
#include "TrainingActive.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/network/WebSocketService.h"
#include "server/api/EvolutionStart.h"
#include "server/api/GenomeGet.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/SeedAdd.h"
#include "server/api/SimRun.h"
#include "server/api/TrainingStreamConfigSet.h"
#include "server/api/UserSettingsSet.h"
#include "ui/TrainingIdleView.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include <algorithm>
#include <lvgl.h>
#include <utility>

namespace DirtSim {
namespace Ui {
namespace State {

TrainingIdle::TrainingIdle(
    TrainingSpec lastTrainingSpec,
    bool hasTrainingSpec,
    std::optional<Starfield::Snapshot> starfieldSnapshot)
    : lastTrainingSpec_(std::move(lastTrainingSpec)),
      hasTrainingSpec_(hasTrainingSpec),
      starfieldSnapshot_(std::move(starfieldSnapshot))
{}

TrainingIdle::~TrainingIdle() = default;

void TrainingIdle::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Training idle state (waiting for start command)");

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    DirtSim::Network::WebSocketServiceInterface* wsService = nullptr;
    if (sm.hasWebSocketService()) {
        wsService = &sm.getWebSocketService();
    }

    view_ = std::make_unique<TrainingIdleView>(
        uiManager,
        sm,
        wsService,
        sm.getUserSettings(),
        starfieldSnapshot_ ? &starfieldSnapshot_.value() : nullptr);
    DIRTSIM_ASSERT(view_, "TrainingIdleView creation failed");

    if (auto* panel = uiManager->getExpandablePanel()) {
        panel->clearContent();
        panel->hide();
        panel->resetWidth();
    }

    IconRail* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setVisible(true);
    iconRail->setLayout(RailLayout::SingleColumn);
    iconRail->setMinimizedAffordanceStyle(IconRail::minimizedAffordanceLeftCenter());
    iconRail->setVisibleIcons(
        { IconId::DUCK, IconId::EVOLUTION, IconId::GENOME_BROWSER, IconId::TRAINING_RESULTS });
    iconRail->deselectAll();

    view_->setEvolutionStarted(false);
    view_->clearPanelContent();
    view_->hidePanel();
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
    DIRTSIM_ASSERT(view_, "TrainingIdleView must exist");
    view_->updateAnimations();
}

bool TrainingIdle::isTrainingResultModalVisible() const
{
    DIRTSIM_ASSERT(view_, "TrainingIdleView must exist");
    return view_->isTrainingResultModalVisible();
}

State::Any TrainingIdle::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "Icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    DIRTSIM_ASSERT(view_, "TrainingIdleView must exist");

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
        view_->hidePanel();
        return std::move(*this);
    }

    view_->showPanel();
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
        case IconId::SETTINGS:
        case IconId::PHYSICS:
        case IconId::PLAY:
        case IconId::SCENARIO:
        case IconId::NONE:
            DIRTSIM_ASSERT(false, "Unexpected icon selection in Training idle");
            break;
    }

    return std::move(*this);
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

    DIRTSIM_ASSERT(view_, "TrainingIdleView must exist");

    if (!sm.hasWebSocketService()) {
        LOG_ERROR(State, "No WebSocketService available");
        return std::move(*this);
    }

    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, cannot start evolution");
        return std::move(*this);
    }

    // Set up training streams BEFORE starting evolution. If evolution starts first, fast training
    // (e.g. 1 generation) can complete before stream setup finishes, deadlocking the server's
    // broadcastTrainingResult against the UI's pending RenderFormatSet request.
    Api::TrainingStreamConfigSet::Command streamCmd{
        .intervalMs = sm.getUserSettings().streamIntervalMs,
        .bestPlaybackEnabled = sm.getUserSettings().bestPlaybackEnabled,
        .bestPlaybackIntervalMs = sm.getUserSettings().bestPlaybackIntervalMs,
    };
    auto streamResult = wsService.sendCommandAndGetResponse<Api::TrainingStreamConfigSet::OkayType>(
        streamCmd, 2000);
    if (streamResult.isError()) {
        LOG_WARN(State, "Pre-start TrainingStreamConfigSet failed: {}", streamResult.errorValue());
    }

    Api::RenderFormatSet::Command renderCmd;
    renderCmd.format = RenderFormat::EnumType::Basic;
    auto renderEnvelope =
        DirtSim::Network::make_command_envelope(wsService.allocateRequestId(), renderCmd);
    auto renderResult = wsService.sendBinaryAndReceive(renderEnvelope);
    if (renderResult.isError()) {
        LOG_WARN(State, "Pre-start RenderFormatSet failed: {}", renderResult.errorValue());
    }

    Api::EvolutionStart::Command cmd;
    cmd.evolution = evt.evolution;
    cmd.mutation = evt.mutation;
    cmd.scenarioId = evt.training.scenarioId;
    cmd.organismType = evt.training.organismType;
    cmd.population = evt.training.population;
    cmd.resumePolicy = evt.resumePolicy;

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

    // Persist training config to server UserSettings for auto-start and restart survival.
    auto serverSettings = sm.getServerUserSettings();
    serverSettings.trainingSpec = evt.training;
    serverSettings.evolutionConfig = evt.evolution;
    serverSettings.mutationConfig = evt.mutation;
    serverSettings.trainingResumePolicy = evt.resumePolicy;
    Api::UserSettingsSet::Command settingsCmd{ .settings = serverSettings };
    auto settingsResult =
        wsService.sendCommandAndGetResponse<Api::UserSettingsSet::Okay>(settingsCmd, 2000);
    if (settingsResult.isError()) {
        LOG_WARN(State, "Failed to persist training config: {}", settingsResult.errorValue());
    }
    else if (settingsResult.value().isError()) {
        LOG_WARN(
            State,
            "Server rejected training config: {}",
            settingsResult.value().errorValue().message);
    }

    starfieldSnapshot_ = view_->captureStarfieldSnapshot();
    return TrainingActive{ lastTrainingSpec_, hasTrainingSpec_, starfieldSnapshot_ };
}

State::Any TrainingIdle::onEvent(const UiApi::TrainingStart::Cwc& cwc, StateMachine& sm)
{
    StartEvolutionButtonClickedEvent evt{
        .evolution = cwc.command.evolution,
        .mutation = cwc.command.mutation,
        .training = cwc.command.training,
        .resumePolicy = cwc.command.resumePolicy,
    };
    auto nextState = onEvent(evt, sm);
    cwc.sendResponse(UiApi::TrainingStart::Response::okay({ .queued = true }));
    return nextState;
}

State::Any TrainingIdle::onEvent(const UiApi::GenomeBrowserOpen::Cwc& cwc, StateMachine& sm)
{
    using Response = UiApi::GenomeBrowserOpen::Response;

    DIRTSIM_ASSERT(view_, "TrainingIdleView must exist");

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    view_->clearPanelContent();
    view_->createGenomeBrowserPanel();

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->selectIcon(IconId::GENOME_BROWSER);

    cwc.sendResponse(Response::okay({ .opened = true }));
    return std::move(*this);
}

State::Any TrainingIdle::onEvent(const UiApi::GenomeDetailOpen::Cwc& cwc, StateMachine& /*sm*/)
{
    using Response = UiApi::GenomeDetailOpen::Response;

    DIRTSIM_ASSERT(view_, "TrainingIdleView must exist");

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

    DIRTSIM_ASSERT(view_, "TrainingIdleView must exist");

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

    DIRTSIM_ASSERT(view_, "TrainingIdleView must exist");

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

State::Any TrainingIdle::onEvent(const TrainingStreamConfigChangedEvent& evt, StateMachine& sm)
{
    auto& settings = sm.getUserSettings();
    settings.streamIntervalMs = std::max(0, evt.intervalMs);
    settings.bestPlaybackEnabled = evt.bestPlaybackEnabled;
    settings.bestPlaybackIntervalMs = std::max(1, evt.bestPlaybackIntervalMs);

    DIRTSIM_ASSERT(view_, "TrainingIdleView must exist");
    view_->setStreamIntervalMs(settings.streamIntervalMs);
    view_->setBestPlaybackEnabled(settings.bestPlaybackEnabled);
    view_->setBestPlaybackIntervalMs(settings.bestPlaybackIntervalMs);

    return std::move(*this);
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

State::Any TrainingIdle::onEvent(const GenomeAddToTrainingClickedEvent& evt, StateMachine& /*sm*/)
{
    DIRTSIM_ASSERT(view_, "TrainingIdleView must exist");

    view_->addGenomeToTraining(evt.genomeId);
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

} // namespace State
} // namespace Ui
} // namespace DirtSim
