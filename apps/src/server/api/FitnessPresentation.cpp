#include "FitnessPresentation.h"

#include "core/ReflectSerializer.h"

namespace DirtSim::Api {

void to_json(nlohmann::json& j, const FitnessPresentationMetric& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, FitnessPresentationMetric& value)
{
    value = ReflectSerializer::from_json<FitnessPresentationMetric>(j);
}

void to_json(nlohmann::json& j, const FitnessPresentationSection& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, FitnessPresentationSection& value)
{
    value = ReflectSerializer::from_json<FitnessPresentationSection>(j);
}

void to_json(nlohmann::json& j, const FitnessPresentation& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, FitnessPresentation& value)
{
    value = ReflectSerializer::from_json<FitnessPresentation>(j);
}

} // namespace DirtSim::Api
