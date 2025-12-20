#include "WebRtcAnswer.h"

namespace DirtSim {
namespace UiApi {
namespace WebRtcAnswer {

nlohmann::json Command::toJson() const
{
    return nlohmann::json{ { "command", "WebRtcAnswer" },
                           { "clientId", clientId },
                           { "sdp", sdp } };
}

Command Command::fromJson(const nlohmann::json& j)
{
    Command cmd;
    if (j.contains("clientId")) {
        cmd.clientId = j["clientId"].get<std::string>();
    }
    if (j.contains("sdp")) {
        cmd.sdp = j["sdp"].get<std::string>();
    }
    return cmd;
}

nlohmann::json Okay::toJson() const
{
    return nlohmann::json{ { "accepted", accepted } };
}

Okay Okay::fromJson(const nlohmann::json& j)
{
    Okay ok;
    if (j.contains("accepted")) {
        ok.accepted = j["accepted"].get<bool>();
    }
    return ok;
}

} // namespace WebRtcAnswer
} // namespace UiApi
} // namespace DirtSim
