#include "TrainingResult.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const TrainingResult& result)
{
    j = ReflectSerializer::to_json(result);
}

void from_json(const nlohmann::json& j, TrainingResult& result)
{
    result = ReflectSerializer::from_json<TrainingResult>(j);
}

void to_json(nlohmann::json& j, const TrainingResult::Summary& summary)
{
    j = ReflectSerializer::to_json(summary);
}

void from_json(const nlohmann::json& j, TrainingResult::Summary& summary)
{
    summary = ReflectSerializer::from_json<TrainingResult::Summary>(j);
}

void to_json(nlohmann::json& j, const TrainingResult::Candidate& candidate)
{
    j = ReflectSerializer::to_json(candidate);
}

void from_json(const nlohmann::json& j, TrainingResult::Candidate& candidate)
{
    candidate = ReflectSerializer::from_json<TrainingResult::Candidate>(j);
}

} // namespace Api
} // namespace DirtSim
