#include "State.h"
#include "core/LoggingChannels.h"
#include "os-manager/OperatingSystemManager.h"

namespace DirtSim {
namespace OsManager {
namespace State {

void Error::onEnter(OperatingSystemManager& /*osm*/)
{
    LOG_ERROR(State, "Entered Error state: {}", error_message);
}

Any Error::onEvent(const OsApi::Reboot::Cwc& cwc, OperatingSystemManager& /*osm*/)
{
    LOG_INFO(State, "Reboot command received in Error state");
    cwc.sendResponse(OsApi::Reboot::Response::okay(std::monostate{}));
    return Rebooting{};
}

Any Error::onEvent(const OsApi::SystemStatus::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "SystemStatus command received in Error state");
    cwc.sendResponse(OsApi::SystemStatus::Response::okay(osm.buildSystemStatus()));
    return *this;
}

} // namespace State
} // namespace OsManager
} // namespace DirtSim
