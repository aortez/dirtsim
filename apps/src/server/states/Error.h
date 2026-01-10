#pragma once

#include "StateForward.h"
#include "server/Event.h"
#include <string>

namespace DirtSim {
namespace Server {
namespace State {

/**
 * @brief Error state - server encountered a configuration or startup error.
 *
 * The server enters this state when it cannot start normally (e.g., missing or
 * invalid config files). It remains running so the UI can connect and display
 * the error message to the user.
 */
struct Error {
    std::string error_message;

    void onEnter(StateMachine& dsm);

    Any onEvent(const Api::Exit::Cwc& cwc, StateMachine& dsm);

    static constexpr const char* name() { return "Error"; }
};

} // namespace State
} // namespace Server
} // namespace DirtSim
