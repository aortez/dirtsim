#include "ScenarioListGet.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {
namespace ScenarioListGet {

void to_json(nlohmann::json& j, const ScenarioInfo& info)
{
    j = ReflectSerializer::to_json(info);
}

void from_json(const nlohmann::json& j, ScenarioInfo& info)
{
    info = ReflectSerializer::from_json<ScenarioInfo>(j);
}

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

nlohmann::json Okay::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

} // namespace ScenarioListGet
} // namespace Api
} // namespace DirtSim
