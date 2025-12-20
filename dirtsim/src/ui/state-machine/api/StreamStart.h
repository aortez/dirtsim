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
 * a peer connection, adding video track, and returning the WebRTC offer.
 */
struct Command {
    std::string clientId; // Unique client identifier (from browser).

    // Populated by WebSocketService - identifies the WebSocket connection for
    // sending follow-up messages (ICE candidates) back to this client.
    std::string connectionId;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);
};

/**
 * @brief Response containing the WebRTC SDP offer.
 *
 * The offer is returned synchronously. ICE candidates will be sent
 * as separate messages via the same WebSocket connection.
 */
struct Okay {
    bool initiated = true;
    std::string sdpOffer; // The WebRTC SDP offer for the browser to answer.

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
