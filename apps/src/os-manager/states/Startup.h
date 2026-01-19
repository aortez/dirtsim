#pragma once

#include "StateForward.h"

namespace DirtSim {
namespace OsManager {
namespace State {

struct Startup {
    Any onEnter(OperatingSystemManager& osm);

    static constexpr const char* name() { return "Startup"; }
};

} // namespace State
} // namespace OsManager
} // namespace DirtSim
