#include "State.h"
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
            cmd.scenarioId = "TreeGermination";

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

    view_ = std::make_unique<TrainingView>(uiManager);
    view_->setStopCallback(onStopClicked, this);
}

void Training::onExit(StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exiting Training state");
    view_.reset();
    sm_ = nullptr;
}

void Training::onStopClicked(lv_event_t* e)
{
    auto* self = static_cast<Training*>(lv_event_get_user_data(e));
    if (!self || !self->sm_) return;

    LOG_INFO(State, "Stop button clicked, sending EvolutionStop");

    // Send EvolutionStop to server.
    if (self->sm_->hasWebSocketService()) {
        auto& wsService = self->sm_->getWebSocketService();
        if (wsService.isConnected()) {
            Api::EvolutionStop::Command cmd;
            const auto result = wsService.sendCommand<Api::EvolutionStop::OkayType>(cmd, 2000);
            if (result.isError()) {
                LOG_ERROR(State, "Failed to send EvolutionStop: {}", result.errorValue());
            }
            else if (result.value().isError()) {
                LOG_ERROR(
                    State, "Server EvolutionStop error: {}", result.value().errorValue().message);
            }
            else {
                LOG_INFO(State, "Evolution stopped on server");
            }
        }
    }

    // Queue transition back to StartMenu.
    self->sm_->queueEvent(StopButtonClickedEvent{});
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

State::Any Training::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Stop button clicked, returning to StartMenu");

    // Transition back to start menu.
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
