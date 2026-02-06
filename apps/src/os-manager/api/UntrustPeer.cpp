#include "os-manager/api/UntrustPeer.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace OsApi {
namespace UntrustPeer {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

nlohmann::json Okay::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

} // namespace UntrustPeer
} // namespace OsApi
} // namespace DirtSim
