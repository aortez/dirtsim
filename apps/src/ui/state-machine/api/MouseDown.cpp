#include "MouseDown.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace UiApi {
namespace MouseDown {

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

} // namespace MouseDown
} // namespace UiApi
} // namespace DirtSim
