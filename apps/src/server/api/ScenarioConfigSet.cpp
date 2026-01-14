#include "ScenarioConfigSet.h"
#include "core/ReflectSerializer.h"
#include "core/VariantSerializer.h"

namespace DirtSim {
namespace Api {
namespace ScenarioConfigSet {

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

} // namespace ScenarioConfigSet
} // namespace Api
} // namespace DirtSim
