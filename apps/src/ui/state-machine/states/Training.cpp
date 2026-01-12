#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "ui/TrainingView.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"

namespace DirtSim {
namespace Ui {
namespace State {

void Training::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Training state, starting evolution on server");
    sm_ = &sm;

    // Send EvolutionStart to server.
    if (sm.hasWebSocketService()) {
        auto& wsService = sm.getWebSocketService();
        if (wsService.isConnected()) {
            Api::EvolutionStart::Command cmd;
            cmd.scenarioId = ScenarioId::TreeGermination;

            const auto result = wsService.sendCommand<Api::EvolutionStart::OkayType>(cmd, 5000);
            if (result.isError()) {
                LOG_ERROR(State, "Failed to send EvolutionStart: {}", result.errorValue());
            }
            else if (result.value().isError()) {
                LOG_ERROR(
                    State, "Server EvolutionStart error: {}", result.value().errorValue().message);
            }
            else {
                LOG_INFO(State, "Evolution started on server");
            }
        }
        else {
            LOG_WARN(State, "Not connected to server, cannot start evolution");
        }
    }

    // Create training view.
    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        LOG_ERROR(State, "No UiComponentManager available");
        return;
    }

    view_ = std::make_unique<TrainingView>(uiManager, sm);
    view_->connectToIconRail();

    IconRail* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setVisibleIcons({ IconId::EVOLUTION });
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

State::Any Training::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state.
    return Shutdown{};
}

State::Any Training::onEvent(const ServerDisconnectedEvent& evt, StateMachine& /*sm*/)
{
    LOG_WARN(State, "Server disconnected during training (reason: {})", evt.reason);
    LOG_INFO(State, "Transitioning to Disconnected");

    // Lost connection - go back to Disconnected state.
    return Disconnected{};
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

    // Update UI progress bars and labels.
    if (view_) {
        view_->updateProgress(progress);
    }

    // Stay in Training state.
    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
