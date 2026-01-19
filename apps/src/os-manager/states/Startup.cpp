#include "State.h"
#include "core/LoggingChannels.h"
#include "os-manager/OperatingSystemManager.h"

namespace DirtSim {
namespace OsManager {
namespace State {

Any Startup::onEnter(OperatingSystemManager& /*osm*/)
{
    LOG_INFO(State, "Transitioning to Idle");
    return Idle{};
}

} // namespace State
} // namespace OsManager
} // namespace DirtSim
