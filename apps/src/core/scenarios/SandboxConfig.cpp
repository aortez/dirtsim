#include "SandboxConfig.h"
#include "core/ReflectSerializer.h"

namespace DirtSim::Config {

void from_json(const nlohmann::json& j, Sandbox& config)
{
    config = ReflectSerializer::from_json<Sandbox>(j);
}

void to_json(nlohmann::json& j, const Sandbox& config)
{
    j = ReflectSerializer::to_json(config);
}

} // namespace DirtSim::Config
