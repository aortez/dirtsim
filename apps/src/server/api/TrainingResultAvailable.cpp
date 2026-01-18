#include "TrainingResultAvailable.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const TrainingResultAvailable::Summary& summary)
{
    j = ReflectSerializer::to_json(summary);
}

void from_json(const nlohmann::json& j, TrainingResultAvailable::Summary& summary)
{
    summary = ReflectSerializer::from_json<TrainingResultAvailable::Summary>(j);
}

void to_json(nlohmann::json& j, const TrainingResultAvailable::Candidate& candidate)
{
    j = ReflectSerializer::to_json(candidate);
}

void from_json(const nlohmann::json& j, TrainingResultAvailable::Candidate& candidate)
{
    candidate = ReflectSerializer::from_json<TrainingResultAvailable::Candidate>(j);
}

} // namespace Api
} // namespace DirtSim
