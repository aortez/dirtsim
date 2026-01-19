#include "RestartServer.h"

namespace DirtSim {
namespace OsApi {
namespace RestartServer {

nlohmann::json Command::toJson() const
{
    return nlohmann::json::object();
}

Command Command::fromJson(const nlohmann::json& /*j*/)
{
    return Command{};
}

} // namespace RestartServer
} // namespace OsApi
} // namespace DirtSim
