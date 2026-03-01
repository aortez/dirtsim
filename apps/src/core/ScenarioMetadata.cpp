#include "core/ScenarioMetadata.h"

#include "core/ReflectSerializer.h"

namespace DirtSim {

void to_json(nlohmann::json& j, const ScenarioMetadata& meta)
{
    j = ReflectSerializer::to_json(meta);
}

void from_json(const nlohmann::json& j, ScenarioMetadata& meta)
{
    meta = ReflectSerializer::from_json<ScenarioMetadata>(j);
}

} // namespace DirtSim
