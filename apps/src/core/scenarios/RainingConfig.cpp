#include "RainingConfig.h"
#include "core/ReflectSerializer.h"

namespace DirtSim::Config {

void from_json(const nlohmann::json& j, Raining& config)
{
    config = ReflectSerializer::from_json<Raining>(j);
}

void to_json(nlohmann::json& j, const Raining& config)
{
    j = ReflectSerializer::to_json(config);
}

} // namespace DirtSim::Config
