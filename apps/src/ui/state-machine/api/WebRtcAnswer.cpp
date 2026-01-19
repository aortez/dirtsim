#include "WebRtcAnswer.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace UiApi {
namespace WebRtcAnswer {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    j.at("clientId");
    j.at("sdp");
    return ReflectSerializer::from_json<Command>(j);
}

nlohmann::json Okay::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Okay Okay::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Okay>(j);
}

} // namespace WebRtcAnswer
} // namespace UiApi
} // namespace DirtSim
