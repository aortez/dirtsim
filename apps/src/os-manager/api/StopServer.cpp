#include "StopServer.h"

namespace DirtSim {
namespace OsApi {
namespace StopServer {

nlohmann::json Command::toJson() const
{
    return nlohmann::json::object();
}

Command Command::fromJson(const nlohmann::json& /*j*/)
{
    return Command{};
}

} // namespace StopServer
} // namespace OsApi
} // namespace DirtSim
