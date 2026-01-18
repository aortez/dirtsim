#include "ClockEventTrigger.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {
namespace ClockEventTrigger {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

} // namespace ClockEventTrigger
} // namespace Api
} // namespace DirtSim
