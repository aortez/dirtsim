#include "StopUi.h"

namespace DirtSim {
namespace OsApi {
namespace StopUi {

nlohmann::json Command::toJson() const
{
    return nlohmann::json::object();
}

Command Command::fromJson(const nlohmann::json& /*j*/)
{
    return Command{};
}

} // namespace StopUi
} // namespace OsApi
} // namespace DirtSim
