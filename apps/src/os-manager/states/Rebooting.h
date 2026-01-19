#pragma once

#include "StateForward.h"

namespace DirtSim {
namespace OsManager {
namespace State {

struct Rebooting {
    void onEnter(OperatingSystemManager& osm);

    static constexpr const char* name() { return "Rebooting"; }
};

} // namespace State
} // namespace OsManager
} // namespace DirtSim
