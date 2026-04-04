#include "PlanSaved.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const PlanSaved& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, PlanSaved& value)
{
    value = ReflectSerializer::from_json<PlanSaved>(j);
}

} // namespace Api
} // namespace DirtSim
