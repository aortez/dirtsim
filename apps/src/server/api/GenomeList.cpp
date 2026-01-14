#include "GenomeList.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {
namespace GenomeList {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

void to_json(nlohmann::json& j, const GenomeEntry& e)
{
    j = ReflectSerializer::to_json(e);
}

void from_json(const nlohmann::json& j, GenomeEntry& e)
{
    e = ReflectSerializer::from_json<GenomeEntry>(j);
}

nlohmann::json Okay::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

} // namespace GenomeList
} // namespace Api
} // namespace DirtSim
