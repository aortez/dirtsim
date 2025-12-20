#include "WebRtcCandidate.h"

namespace DirtSim {
namespace UiApi {
namespace WebRtcCandidate {

nlohmann::json Command::toJson() const
{
    return nlohmann::json{ { "command", "WebRtcCandidate" },
                           { "clientId", clientId },
                           { "candidate", candidate },
                           { "mid", mid } };
}

Command Command::fromJson(const nlohmann::json& j)
{
    Command cmd;
    if (j.contains("clientId")) {
        cmd.clientId = j["clientId"].get<std::string>();
    }
    if (j.contains("candidate")) {
        cmd.candidate = j["candidate"].get<std::string>();
    }
    if (j.contains("mid")) {
        cmd.mid = j["mid"].get<std::string>();
    }
    return cmd;
}

nlohmann::json Okay::toJson() const
{
    return nlohmann::json{ { "added", added } };
}

Okay Okay::fromJson(const nlohmann::json& j)
{
    Okay ok;
    if (j.contains("added")) {
        ok.added = j["added"].get<bool>();
    }
    return ok;
}

} // namespace WebRtcCandidate
} // namespace UiApi
} // namespace DirtSim
