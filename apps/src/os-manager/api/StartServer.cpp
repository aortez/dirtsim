#include "StartServer.h"

namespace DirtSim {
namespace OsApi {
namespace StartServer {

nlohmann::json Command::toJson() const
{
    return nlohmann::json::object();
}

Command Command::fromJson(const nlohmann::json& /*j*/)
{
    return Command{};
}

} // namespace StartServer
} // namespace OsApi
} // namespace DirtSim
