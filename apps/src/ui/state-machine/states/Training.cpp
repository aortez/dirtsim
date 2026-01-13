#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "server/api/RenderFormatSet.h"
#include "ui/TrainingView.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include <atomic>

namespace DirtSim {
namespace Ui {
namespace State {

void Training::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Training state (waiting for start command)");
    sm_ = &sm;
    evolutionStarted_ = false;

    // Create training view.
    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        LOG_ERROR(State, "No UiComponentManager available");
        return;
    }

    view_ = std::make_unique<TrainingView>(uiManager, sm);

    IconRail* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setVisibleIcons({ IconId::CORE, IconId::EVOLUTION });
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
    sm_ = nullptr;
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

State::Any Training::onEvent(const IconSelectedEvent& evt, StateMachine& /*sm*/)
{
    LOG_INFO(
        State,
        "Icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    // Forward to TrainingView to handle panel content.
    if (view_) {
        view_->onIconSelected(evt.selectedId, evt.previousId);
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

State::Any Training::onEvent(const ServerDisconnectedEvent& evt, StateMachine& /*sm*/)
{
    LOG_WARN(State, "Server disconnected during training (reason: {})", evt.reason);
    LOG_INFO(State, "Transitioning to Disconnected");

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
        "Starting evolution: population={}, generations={}",
        evt.evolution.populationSize,
        evt.evolution.maxGenerations);

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
    cmd.scenarioId = Scenario::EnumType::TreeGermination;
    cmd.evolution = evt.evolution;
    cmd.mutation = evt.mutation;

    const auto result = wsService.sendCommand<Api::EvolutionStart::OkayType>(cmd, 5000);
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
    }

    return std::move(*this);
}

State::Any Training::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Stop button clicked, stopping evolution");

    DIRTSIM_ASSERT(sm.hasWebSocketService(), "WebSocketService must exist");
    auto& wsService = sm.getWebSocketService();
    DIRTSIM_ASSERT(wsService.isConnected(), "Must be connected to reach Training state");

    Api::EvolutionStop::Command cmd;
    const auto result = wsService.sendCommand<Api::EvolutionStop::OkayType>(cmd, 2000);
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

State::Any Training::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state.
    return Shutdown{};
}

State::Any Training::onEvent(const UiUpdateEvent& evt, StateMachine& /*sm*/)
{
    // Render live training world.
    if (view_) {
        view_->renderWorld(evt.worldData);
    }

    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
