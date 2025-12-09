#pragma once

#include "StateForward.h"
#include "ui/state-machine/Event.h"

namespace DirtSim {
namespace Ui {
namespace State {

/**
 * @brief Disconnected state - no DSSM server connection.
 */
struct Disconnected {

    Any onEvent(const ConnectToServerCommand& cmd, StateMachine& sm);
    Any onEvent(const ServerConnectedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "Disconnected"; }
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
