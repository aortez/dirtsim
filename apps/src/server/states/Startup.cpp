#include "State.h"
#include "core/LoggingChannels.h"
#include "server/StateMachine.h"

namespace DirtSim {
namespace Server {
namespace State {

Any Startup::onEnter(StateMachine& dsm)
{
    (void)dsm;
    LOG_INFO(State, "Server startup complete");
    LOG_INFO(State, "Transitioning to Idle");

    return Idle{};
}

} // namespace State
} // namespace Server
} // namespace DirtSim
