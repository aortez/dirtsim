#include "os-manager/api/PeerClientKeyEnsure.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace OsApi {
namespace PeerClientKeyEnsure {

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

} // namespace PeerClientKeyEnsure
} // namespace OsApi
} // namespace DirtSim
