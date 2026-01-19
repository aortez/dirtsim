#include "MouseUp.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace UiApi {
namespace MouseUp {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    j.at("pixelX");
    j.at("pixelY");
    return ReflectSerializer::from_json<Command>(j);
}

} // namespace MouseUp
} // namespace UiApi
} // namespace DirtSim
