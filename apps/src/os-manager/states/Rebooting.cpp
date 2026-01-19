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

void Rebooting::onEnter(OperatingSystemManager& osm)
{
    LOG_INFO(State, "Stopping services before reboot");

    auto uiStop = osm.stopService(kUiService);
    if (uiStop.isError()) {
        LOG_WARN(State, "Failed to stop UI service: {}", uiStop.errorValue().message);
    }

    auto serverStop = osm.stopService(kServerService);
    if (serverStop.isError()) {
        LOG_WARN(State, "Failed to stop server service: {}", serverStop.errorValue().message);
    }

    osm.scheduleReboot();
    osm.requestExit();
}

} // namespace State
} // namespace OsManager
} // namespace DirtSim
