#include "GlowConfig.h"
#include "core/ReflectSerializer.h"
#include <nlohmann/json.hpp>

namespace DirtSim {

void to_json(nlohmann::json& j, const GlowConfig& config)
{
    j = ReflectSerializer::to_json(config);
}

void from_json(const nlohmann::json& j, GlowConfig& config)
{
    config = ReflectSerializer::from_json<GlowConfig>(j);
}

} // namespace DirtSim
