#include "SimPause.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace UiApi {
namespace SimPause {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

nlohmann::json Okay::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

} // namespace SimPause
} // namespace UiApi
} // namespace DirtSim
