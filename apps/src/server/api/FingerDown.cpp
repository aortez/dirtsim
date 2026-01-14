#include "FingerDown.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {
namespace FingerDown {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

} // namespace FingerDown
} // namespace Api
} // namespace DirtSim
