#pragma once

#include "StateForward.h"
#include "os-manager/Event.h"
#include <string>

namespace DirtSim {
namespace OsManager {
namespace State {

struct Error {
    std::string error_message;

    void onEnter(OperatingSystemManager& osm);

    Any onEvent(const OsApi::Reboot::Cwc& cwc, OperatingSystemManager& osm);
    Any onEvent(const OsApi::SystemStatus::Cwc& cwc, OperatingSystemManager& osm);

    static constexpr const char* name() { return "Error"; }
};

} // namespace State
} // namespace OsManager
} // namespace DirtSim
