#include "FitnessBreakdownReport.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const FitnessMetric& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, FitnessMetric& value)
{
    value = ReflectSerializer::from_json<FitnessMetric>(j);
}

void to_json(nlohmann::json& j, const FitnessBreakdownReport& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, FitnessBreakdownReport& value)
{
    value = ReflectSerializer::from_json<FitnessBreakdownReport>(j);
}

} // namespace Api
} // namespace DirtSim
