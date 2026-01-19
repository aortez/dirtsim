#include "StartServer.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace OsApi {
namespace StartServer {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

} // namespace StartServer
} // namespace OsApi
} // namespace DirtSim
