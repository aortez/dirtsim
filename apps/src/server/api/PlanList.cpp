#include "PlanList.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {
namespace PlanList {

void to_json(nlohmann::json& j, const Entry& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, Entry& value)
{
    value = ReflectSerializer::from_json<Entry>(j);
}

} // namespace PlanList
} // namespace Api
} // namespace DirtSim
