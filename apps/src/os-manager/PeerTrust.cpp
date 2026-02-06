#include "os-manager/PeerTrust.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace OsManager {

void to_json(nlohmann::json& j, const PeerTrustBundle& bundle)
{
    j = ReflectSerializer::to_json(bundle);
}

void from_json(const nlohmann::json& j, PeerTrustBundle& bundle)
{
    bundle = ReflectSerializer::from_json<PeerTrustBundle>(j);
}

} // namespace OsManager
} // namespace DirtSim
