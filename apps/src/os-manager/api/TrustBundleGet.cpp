#include "os-manager/api/TrustBundleGet.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace OsApi {
namespace TrustBundleGet {

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

} // namespace TrustBundleGet
} // namespace OsApi
} // namespace DirtSim
