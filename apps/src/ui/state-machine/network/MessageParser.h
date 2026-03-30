#pragma once

#include "core/RenderMessageFull.h"
#include "ui/state-machine/Event.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace DirtSim {
namespace Ui {

/**
 * @brief Parses WebSocket messages from DSSM server into UI events.
 *
 * This is a state-independent message parser that converts JSON messages
 * into strongly-typed events for the UI state machine. It handles responses
 * (state_get, errors).
 */
class MessageParser {
public:
    /**
     * @brief Parse a WebSocket message into a UI event.
     * @param message Raw JSON string from DSSM server.
     * @return Parsed event if successful, nullopt if unknown/invalid message.
     */
    static std::optional<Event> parse(const std::string& message);

    /**
     * @brief Parse a server-pushed command broadcast into a UI event.
     */
    static std::optional<Event> parseServerCommand(
        const std::string& messageType, const std::vector<std::byte>& payload);

    /**
     * @brief Parse a binary RenderMessage push into a UiUpdateEvent.
     */
    static UiUpdateEvent parseRenderMessage(const std::vector<std::byte>& bytes);

private:
    /**
     * @brief Try to parse as a state_get response with WorldData.
     */
    static std::optional<Event> parseWorldDataResponse(const nlohmann::json& json);

    /**
     * @brief Handle error responses.
     */
    static void handleError(const nlohmann::json& json);

    static UiUpdateEvent parseRenderMessageFull(const RenderMessageFull& fullMsg);
    static void validateRenderMessage(const RenderMessage& renderMsg);
};

} // namespace Ui
} // namespace DirtSim
