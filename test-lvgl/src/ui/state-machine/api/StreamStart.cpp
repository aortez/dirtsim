#include "StreamStart.h"

namespace DirtSim {
namespace UiApi {
namespace StreamStart {

nlohmann::json Command::toJson() const
{
    return nlohmann::json{ { "command", "StreamStart" }, { "clientId", clientId } };
}

Command Command::fromJson(const nlohmann::json& j)
{
    Command cmd;
    if (j.contains("clientId")) {
        cmd.clientId = j["clientId"].get<std::string>();
    }
    return cmd;
}

nlohmann::json Okay::toJson() const
{
    return nlohmann::json{ { "initiated", initiated }, { "sdpOffer", sdpOffer } };
}

Okay Okay::fromJson(const nlohmann::json& j)
{
    Okay ok;
    if (j.contains("initiated")) {
        ok.initiated = j["initiated"].get<bool>();
    }
    if (j.contains("sdpOffer")) {
        ok.sdpOffer = j["sdpOffer"].get<std::string>();
    }
    return ok;
}

} // namespace StreamStart
} // namespace UiApi
} // namespace DirtSim
