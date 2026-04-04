#include "Plan.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const PlanSummary& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, PlanSummary& value)
{
    value = ReflectSerializer::from_json<PlanSummary>(j);
}

void to_json(nlohmann::json& j, const SmbPlaybackRoot& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, SmbPlaybackRoot& value)
{
    value = ReflectSerializer::from_json<SmbPlaybackRoot>(j);
}

void to_json(nlohmann::json& j, const Plan& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, Plan& value)
{
    value = ReflectSerializer::from_json<Plan>(j);
}

} // namespace Api
} // namespace DirtSim
