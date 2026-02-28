#include "TreeGerminationConfig.h"
#include "core/ReflectSerializer.h"

namespace DirtSim::Config {

void from_json(const nlohmann::json& j, TreeGermination& config)
{
    config = ReflectSerializer::from_json<TreeGermination>(j);
}

void to_json(nlohmann::json& j, const TreeGermination& config)
{
    j = ReflectSerializer::to_json(config);
}

} // namespace DirtSim::Config
