#include "State.h"
#include "core/LoggingChannels.h"
#include "ui/state-machine/StateMachine.h"

namespace DirtSim {
namespace Ui {
namespace State {

State::Any Startup::onEvent(const InitCompleteEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "LVGL and display systems initialized");
    LOG_INFO(State, "Transitioning to Disconnected (no server connection)");

    // Transition to Disconnected state (show connection UI).
    return Disconnected{};
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
