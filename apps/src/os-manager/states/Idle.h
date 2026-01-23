#pragma once

#include "StateForward.h"
#include "os-manager/Event.h"

namespace DirtSim {
namespace OsManager {
namespace State {

struct Idle {
    void onEnter(OperatingSystemManager& osm);
    void onExit(OperatingSystemManager& osm);

    Any onEvent(const OsApi::Reboot::Cwc& cwc, OperatingSystemManager& osm);
    Any onEvent(const OsApi::RestartServer::Cwc& cwc, OperatingSystemManager& osm);
    Any onEvent(const OsApi::RestartUi::Cwc& cwc, OperatingSystemManager& osm);
    Any onEvent(const OsApi::StartServer::Cwc& cwc, OperatingSystemManager& osm);
    Any onEvent(const OsApi::StartUi::Cwc& cwc, OperatingSystemManager& osm);
    Any onEvent(const OsApi::StopServer::Cwc& cwc, OperatingSystemManager& osm);
    Any onEvent(const OsApi::StopUi::Cwc& cwc, OperatingSystemManager& osm);
    Any onEvent(const OsApi::SystemStatus::Cwc& cwc, OperatingSystemManager& osm);
    Any onEvent(const OsApi::WebUiAccessSet::Cwc& cwc, OperatingSystemManager& osm);

    static constexpr const char* name() { return "Idle"; }
};

} // namespace State
} // namespace OsManager
} // namespace DirtSim
