#include "FingerUp.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {
namespace FingerUp {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

} // namespace FingerUp
} // namespace Api
} // namespace DirtSim
