#include "SynthKeyEvent.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace UiApi {
namespace SynthKeyEvent {

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

} // namespace SynthKeyEvent
} // namespace UiApi
} // namespace DirtSim
