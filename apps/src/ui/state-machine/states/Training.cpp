#include "State.h"
#include "core/LoggingChannels.h"
#include "ui/state-machine/StateMachine.h"

namespace DirtSim {
namespace Ui {
namespace State {

void Training::onEnter(StateMachine& /*sm*/)
{
    LOG_INFO(State, "Entering Training state");

    // TODO: Create training UI with progress bars and controls.
}

void Training::onExit(StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exiting Training state");

    // TODO: Clean up training UI.
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

} // namespace State
} // namespace Ui
} // namespace DirtSim
