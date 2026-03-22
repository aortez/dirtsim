#include "EvolutionProgress.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const EvolutionBreedingTelemetry& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, EvolutionBreedingTelemetry& value)
{
    value = ReflectSerializer::from_json<EvolutionBreedingTelemetry>(j);
}

nlohmann::json EvolutionProgress::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

} // namespace Api
} // namespace DirtSim
