#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"

#include <nlohmann/json.hpp>
#include <string>

namespace DirtSim {
namespace UiApi {

namespace WebRtcAnswer {

DEFINE_API_NAME(WebRtcAnswer);

/**
 * @brief Browser's answer to server's WebRTC offer.
 */
struct Command {
    std::string clientId; // Unique client identifier.
    std::string sdp;      // SDP answer from browser.

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);
};

/**
 * @brief Acknowledgment response.
 */
struct Okay {
    bool accepted = true;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);
};

using OkayType = Okay;
using Response = Result<OkayType, DirtSim::ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace WebRtcAnswer
} // namespace UiApi
} // namespace DirtSim
