#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"

#include <nlohmann/json.hpp>
#include <string>

namespace DirtSim {
namespace UiApi {

namespace StreamStart {

DEFINE_API_NAME(StreamStart);

/**
 * @brief Command to initiate WebRTC video streaming.
 *
 * Browser sends this to request a video stream. Server responds by creating
 * a peer connection, adding video track, and sending a WebRTC offer back.
 */
struct Command {
    std::string clientId; // Unique client identifier.

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);
};

/**
 * @brief Response - server will send offer via separate WebSocket message.
 */
struct Okay {
    bool initiated = true;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);
};

using OkayType = Okay;
using Response = Result<OkayType, DirtSim::ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace StreamStart
} // namespace UiApi
} // namespace DirtSim
