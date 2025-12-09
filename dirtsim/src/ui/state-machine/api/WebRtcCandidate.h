#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"

#include <nlohmann/json.hpp>
#include <string>

namespace DirtSim {
namespace UiApi {

namespace WebRtcCandidate {

DEFINE_API_NAME(WebRtcCandidate);

/**
 * @brief Command to add ICE candidate from browser.
 */
struct Command {
    std::string clientId;  // Unique client identifier.
    std::string candidate; // ICE candidate string.
    std::string mid;       // Media stream ID (sdpMid).

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);
};

/**
 * @brief Simple acknowledgment response.
 */
struct Okay {
    bool added = true;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);
};

using OkayType = Okay;
using Response = Result<OkayType, DirtSim::ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace WebRtcCandidate
} // namespace UiApi
} // namespace DirtSim
