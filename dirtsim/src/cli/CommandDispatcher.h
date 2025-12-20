#pragma once

#include "core/ReflectSerializer.h"
#include "core/network/WebSocketService.h"
#include "server/api/ApiCommand.h"
#include "server/api/ApiError.h"
#include "ui/state-machine/api/UiApiCommand.h"
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace DirtSim {
namespace Client {

/**
 * @brief Target type for command dispatch.
 */
enum class Target { Server, Ui };

/**
 * @brief Generic command dispatcher for type-safe WebSocket command execution.
 *
 * Builds a runtime dispatch table from compile-time command types.
 * Maintains separate handler maps for server and UI commands, supporting
 * commands with the same name but different response types.
 */
class CommandDispatcher {
public:
    /**
     * @brief Handler function signature.
     *
     * Takes a WebSocketClient and JSON body, returns JSON response string.
     */
    using Handler = std::function<Result<std::string, ApiError>(
        Network::WebSocketService&, const nlohmann::json&)>;

    /**
     * @brief Construct dispatcher and register all known command types.
     */
    CommandDispatcher();

    /**
     * @brief Dispatch command by name using type-safe execution.
     *
     * @param target Target (Server or Ui) to select handler map.
     * @param client Connected WebSocketClient.
     * @param commandName Command name (e.g., "StateGet", "StatusGet").
     * @param body JSON command body (can be empty for commands with no fields).
     * @return Result with JSON response string on success, ApiError on failure.
     */
    Result<std::string, ApiError> dispatch(
        Target target,
        Network::WebSocketService& client,
        const std::string& commandName,
        const nlohmann::json& body);

    /**
     * @brief Check if a command name is registered for the given target.
     */
    bool hasCommand(Target target, const std::string& commandName) const;

    /**
     * @brief Get list of all registered command names for a target.
     */
    std::vector<std::string> getCommandNames(Target target) const;

private:
    using HandlerMap = std::map<std::string, Handler>;

    /**
     * @brief Register command with both Command and Okay types for full response deserialization.
     *
     * @param handlers Handler map to register into (serverHandlers_ or uiHandlers_).
     */
    template <typename CommandT, typename OkayT>
    void registerCommand(HandlerMap& handlers)
    {
        std::string cmdName(CommandT::name());
        handlers[cmdName] =
            [cmdName](Network::WebSocketService& client, const nlohmann::json& body) {
                // Deserialize JSON body → typed command.
                CommandT cmd;
                if (!body.empty()) {
                    try {
                        cmd = CommandT::fromJson(body);
                    }
                    catch (const std::exception& e) {
                        return Result<std::string, ApiError>::error(
                            ApiError{ std::string("Failed to parse command body: ") + e.what() });
                    }
                }

                // Build binary envelope with command.
                static std::atomic<uint64_t> nextId{ 1 };
                uint64_t id = nextId.fetch_add(1);
                auto envelope = Network::make_command_envelope(id, cmd);

                // Send binary envelope and receive binary response.
                auto envelopeResult = client.sendBinaryAndReceive(envelope);
                if (envelopeResult.isError()) {
                    return Result<std::string, ApiError>::error(
                        ApiError{ envelopeResult.errorValue() });
                }

                // Deserialize typed response from envelope.
                const auto& responseEnvelope = envelopeResult.value();
                try {
                    auto result = Network::extract_result<OkayT, ApiError>(responseEnvelope);

                    if (result.isError()) {
                        nlohmann::json errorJson;
                        errorJson["error"] = result.errorValue().message;
                        errorJson["id"] = responseEnvelope.id;
                        return Result<std::string, ApiError>::okay(errorJson.dump());
                    }

                    // Success - convert typed response to JSON for display.
                    nlohmann::json resultJson;
                    if constexpr (std::is_same_v<OkayT, std::monostate>) {
                        resultJson["success"] = true;
                    }
                    else if constexpr (requires(const OkayT& o) { o.toJson(); }) {
                        // Type has toJson() method - use it directly.
                        resultJson["value"] = result.value().toJson();
                    }
                    else {
                        // Use ReflectSerializer for simple aggregates.
                        resultJson["value"] = ReflectSerializer::to_json(result.value());
                    }
                    resultJson["id"] = responseEnvelope.id;
                    return Result<std::string, ApiError>::okay(resultJson.dump());
                }
                catch (const std::exception& e) {
                    return Result<std::string, ApiError>::error(
                        ApiError{ std::string("Failed to deserialize response: ") + e.what() });
                }
            };
    }

    const HandlerMap& getHandlers(Target target) const;

    HandlerMap serverHandlers_;
    HandlerMap uiHandlers_;
};

} // namespace Client
} // namespace DirtSim
