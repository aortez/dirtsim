#include "TreeSensoryData.h"

#include "core/ReflectSerializer.h"

namespace DirtSim {

void to_json(nlohmann::json& j, const GrowthStage& stage)
{
    j = static_cast<uint8_t>(stage);
}

void from_json(const nlohmann::json& j, GrowthStage& stage)
{
    stage = static_cast<GrowthStage>(j.get<uint8_t>());
}

void to_json(nlohmann::json& j, const TreeSensoryData& data)
{
    j = ReflectSerializer::to_json(data);
}

void from_json(const nlohmann::json& j, TreeSensoryData& data)
{
    data = ReflectSerializer::from_json<TreeSensoryData>(j);
}

} // namespace DirtSim
