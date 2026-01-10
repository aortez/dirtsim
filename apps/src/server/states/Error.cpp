#include "State.h"
#include "core/LoggingChannels.h"
#include "server/StateMachine.h"

namespace DirtSim {
namespace Server {
namespace State {

void Error::onEnter(StateMachine& /*dsm*/)
{
    LOG_ERROR(State, "Server in error state: {}", error_message);
}

State::Any Error::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state.
    return Shutdown{};
}

} // namespace State
} // namespace Server
} // namespace DirtSim
