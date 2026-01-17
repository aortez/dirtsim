#include "TrainingResultGet.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {
namespace TrainingResultGet {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

void to_json(nlohmann::json& j, const Summary& summary)
{
    j = ReflectSerializer::to_json(summary);
}

void from_json(const nlohmann::json& j, Summary& summary)
{
    summary = ReflectSerializer::from_json<Summary>(j);
}

void to_json(nlohmann::json& j, const Candidate& candidate)
{
    j = ReflectSerializer::to_json(candidate);
}

void from_json(const nlohmann::json& j, Candidate& candidate)
{
    candidate = ReflectSerializer::from_json<Candidate>(j);
}

nlohmann::json Okay::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

} // namespace TrainingResultGet
} // namespace Api
} // namespace DirtSim
