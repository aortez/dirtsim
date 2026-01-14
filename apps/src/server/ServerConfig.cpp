#include "ServerConfig.h"
#include "core/ReflectSerializer.h"
#include "core/VariantSerializer.h"

namespace DirtSim {

void from_json(const nlohmann::json& j, ServerConfig& cfg)
{
    cfg = ReflectSerializer::from_json<ServerConfig>(j);
}

void to_json(nlohmann::json& j, const ServerConfig& cfg)
{
    j = ReflectSerializer::to_json(cfg);
}

} // namespace DirtSim
