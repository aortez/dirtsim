#include "PlantSeed.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace UiApi {
namespace PlantSeed {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

} // namespace PlantSeed
} // namespace UiApi
} // namespace DirtSim
