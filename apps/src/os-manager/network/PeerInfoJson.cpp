#include "core/ReflectSerializer.h"
#include "os-manager/network/PeerDiscovery.h"

namespace DirtSim {
namespace OsManager {

void to_json(nlohmann::json& j, const PeerInfo& info)
{
    j = ReflectSerializer::to_json(info);
}

void from_json(const nlohmann::json& j, PeerInfo& info)
{
    info = ReflectSerializer::from_json<PeerInfo>(j);
}

} // namespace OsManager
} // namespace DirtSim
