#include "PlanPlaybackStopped.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const PlanPlaybackStopped& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, PlanPlaybackStopped& value)
{
    value = ReflectSerializer::from_json<PlanPlaybackStopped>(j);
}

} // namespace Api
} // namespace DirtSim
