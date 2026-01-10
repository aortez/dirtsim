#pragma once

#include "StateForward.h"

namespace DirtSim {
namespace Server {
namespace State {

/**
 * @brief Initial startup state - loads config and transitions to Idle or Error.
 */
struct Startup {
    Any onEnter(StateMachine& dsm);

    static constexpr const char* name() { return "Startup"; }
};

} // namespace State
} // namespace Server
} // namespace DirtSim
