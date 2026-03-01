#include "ScenarioListGet.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {
namespace ScenarioListGet {

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
