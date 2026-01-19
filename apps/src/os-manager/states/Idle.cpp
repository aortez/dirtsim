#include "State.h"
#include "core/LoggingChannels.h"
#include "os-manager/OperatingSystemManager.h"

namespace DirtSim {
namespace OsManager {
namespace State {

namespace {

constexpr const char* kServerService = "dirtsim-server.service";
constexpr const char* kUiService = "dirtsim-ui.service";

} // namespace

void Idle::onEnter(OperatingSystemManager& /*osm*/)
{
    LOG_INFO(State, "Idle state ready for commands");
}

void Idle::onExit(OperatingSystemManager& /*osm*/)
{
    LOG_INFO(State, "Exiting Idle state");
}

Any Idle::onEvent(const OsApi::Reboot::Cwc& cwc, OperatingSystemManager& /*osm*/)
{
    LOG_INFO(State, "Reboot command received");
    cwc.sendResponse(OsApi::Reboot::Response::okay(std::monostate{}));
    return Rebooting{};
}

Any Idle::onEvent(const OsApi::RestartServer::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "RestartServer command received");
    cwc.sendResponse(osm.restartService(kServerService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::RestartUi::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "RestartUi command received");
    cwc.sendResponse(osm.restartService(kUiService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::StartServer::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "StartServer command received");
    cwc.sendResponse(osm.startService(kServerService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::StartUi::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "StartUi command received");
    cwc.sendResponse(osm.startService(kUiService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::StopServer::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "StopServer command received");
    cwc.sendResponse(osm.stopService(kServerService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::StopUi::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "StopUi command received");
    cwc.sendResponse(osm.stopService(kUiService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::SystemStatus::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "SystemStatus command received");
    cwc.sendResponse(OsApi::SystemStatus::Response::okay(osm.buildSystemStatus()));
    return Idle{};
}

} // namespace State
} // namespace OsManager
} // namespace DirtSim
