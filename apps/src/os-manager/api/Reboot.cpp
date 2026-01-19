#include "Reboot.h"

namespace DirtSim {
namespace OsApi {
namespace Reboot {

nlohmann::json Command::toJson() const
{
    return nlohmann::json::object();
}

Command Command::fromJson(const nlohmann::json& /*j*/)
{
    return Command{};
}

} // namespace Reboot
} // namespace OsApi
} // namespace DirtSim
