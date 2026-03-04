#include "DuckSensoryData.h"

#include "core/ReflectSerializer.h"

namespace DirtSim {

void to_json(nlohmann::json& j, const DuckSensoryData& data)
{
    j = ReflectSerializer::to_json(data);
}

void from_json(const nlohmann::json& j, DuckSensoryData& data)
{
    data = ReflectSerializer::from_json<DuckSensoryData>(j);
}

} // namespace DirtSim
