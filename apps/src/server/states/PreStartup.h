#pragma once

#include "StateForward.h"

namespace DirtSim {
namespace Server {
namespace State {

/**
 * @brief Initial state before the main loop starts.
 *
 * This is the FSM's initial state. The main loop transitions to Startup,
 * which goes through the normal framework path (onExit -> onEnter chaining).
 */
struct PreStartup {
    static constexpr const char* name() { return "PreStartup"; }
};

} // namespace State
} // namespace Server
} // namespace DirtSim
