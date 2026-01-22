#pragma once

#include "BinaryProtocol.h"
#include "WebSocketServiceInterface.h"
#include "core/CommandWithCallback.h"
#include "core/RenderMessage.h"
#include "core/Result.h"
#include "core/Timers.h"
#include "core/network/ClientHello.h"
#include "core/network/JsonProtocol.h"
#include "server/api/ApiCommand.h"
#include "server/api/ApiError.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <rtc/rtc.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <variant>

namespace DirtSim {

class World;
struct WorldData;

namespace Network {

/**
 * @brief Protocol format for command/response.
 */
enum class Protocol {
    BINARY, // zpp_bits serialization (fast, compact).
    JSON    // JSON serialization (human-readable, debuggable).
};

/**
 * @brief Unified WebSocket service supporting both client and server roles.
 *
 * Can simultaneously act as:
 * - Client: Connect to remote endpoints, send commands, receive responses
 * - Server: Listen for connections, handle incoming commands via registered handlers
 *
 * Supports binary (zpp_bits) protocol by default. JSON available for debugging/CLI.
 *
 * Features:
 * - Result<> return types for proper error handling
 * - Type-safe command templates with automatic name derivation
 * - Correlation ID support for multiplexed requests
 * - Template-based handler registration (server side)
 * - Async callbacks for unsolicited messages
 */
class WebSocketService : public WebSocketServiceInterface {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using BinaryCallback = std::function<void(const std::vector<std::byte>&)>;
    using ConnectionCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using ClientDisconnectCallback = std::function<void(const std::string& connectionId)>;
    using ServerCommandCallback =
        std::function<void(const std::string& messageType, const std::vector<std::byte>& payload)>;
    // Generic JSON deserializer - returns std::any to support both Server and UI command variants.
    using JsonDeserializer = std::function<std::any(const std::string&)>;

    // Helper passed to JSON dispatcher for invoking registered handlers.
    using HandlerInvoker = std::function<void(
        std::string commandName, std::vector<std::byte> payload, uint64_t correlationId)>;

    // JSON dispatcher receives command variant (as std::any) and handler invoker.
    // It visits the variant, creates typed CWCs with JSON callbacks, and invokes handlers.
    using JsonCommandDispatcher = std::function<void(
        std::any cmdVariant,
        std::shared_ptr<rtc::WebSocket> ws,
        uint64_t correlationId,
        HandlerInvoker invokeHandler)>;

    WebSocketService();
    virtual ~WebSocketService();

    WebSocketService(const WebSocketService&) = delete;
    WebSocketService& operator=(const WebSocketService&) = delete;

    Result<std::monostate, std::string> connect(
        const std::string& url, int timeoutMs = 5000) override;

    void disconnect() override;

    bool isConnected() const override;

    std::string getUrl() const override { return url_; }

    void setProtocol(Protocol protocol) { protocol_ = protocol; }

    Protocol getProtocol() const { return protocol_; }

    uint64_t allocateRequestId() override
    {
        return nextId_.fetch_add(1, std::memory_order_relaxed);
    }

    template <ApiCommandType CommandT>
    Result<typename CommandT::OkayType, std::string> sendCommandAndGetResponse(
        const CommandT& cmd, int timeoutMs = 5000)
    {
        if (protocol_ == Protocol::BINARY) {
            return sendCommandBinary<CommandT>(cmd, timeoutMs);
        }
        else {
            return sendCommandJson<CommandT>(cmd, timeoutMs);
        }
    }

    template <typename CommandT>
    Result<std::monostate, std::string> sendCommand(const CommandT& cmd)
    {
        auto envelope = make_command_envelope(0, cmd);
        return sendBinaryToDefaultPeer(serialize_envelope(envelope));
    }

    // =========================================================================
    // Raw send (for advanced use cases and dynamic dispatch).
    // =========================================================================

    /**
     * @brief Send raw text message (fire-and-forget - prefer sync over this).
     */
    Result<std::monostate, std::string> sendText(const std::string& message);

    /**
     * @brief Send raw binary message (fire-and-forget - prefer sync over this).
     */
    Result<std::monostate, std::string> sendBinary(const std::vector<std::byte>& data) override;

    /**
     * @brief Send typed command and receive typed response (recommended).
     *
     * Handles envelope creation, request ID management, and response extraction automatically.
     *
     * @tparam Okay The success response type (e.g., Api::SimRun::Okay).
     * @tparam Command The command type (e.g., Api::SimRun::Command).
     * @param cmd The command to send.
     * @param timeoutMs Timeout in milliseconds.
     * @return Result with Result<Okay, ApiError> on success, error string on failure.
     */
    template <typename Okay, typename Command>
    Result<Result<Okay, ApiError>, std::string> sendCommandAndGetResponse(
        const Command& cmd, int timeoutMs = 5000)
    {
        // Generate unique request ID.
        uint64_t requestId = allocateRequestId();

        // Create envelope.
        auto envelope = make_command_envelope(requestId, cmd);

        // Send and receive.
        auto envResult = sendBinaryAndReceive(envelope, timeoutMs);
        if (envResult.isError()) {
            return Result<Result<Okay, ApiError>, std::string>::error(envResult.errorValue());
        }

        // Extract typed response.
        auto response = extract_result<Okay, ApiError>(envResult.value());
        if (response.isError()) {
            return Result<Result<Okay, ApiError>, std::string>::okay(
                Result<Okay, ApiError>::error(response.errorValue()));
        }

        // Return success response.
        return Result<Result<Okay, ApiError>, std::string>::okay(
            Result<Okay, ApiError>::okay(response.value()));
    }

    /**
     * @brief Send JSON and receive response (for dynamic dispatch).
     *
     * Useful when command type isn't known at compile time (e.g., CLI parsing strings).
     * For type-safe usage, prefer sendCommandAndGetResponse<T>().
     *
     * @param message JSON message to send.
     * @param timeoutMs Timeout in milliseconds.
     * @return Result with JSON response string on success, ApiError on failure.
     */
    Result<std::string, ApiError> sendJsonAndReceive(
        const std::string& message, int timeoutMs = 5000);

    /**
     * @brief Send binary envelope and receive response (for manual testing/incorrect use).
     *
     * Useful for testing binary protocol or when you need full control.
     *
     * @param envelope The message envelope to send.
     * @param timeoutMs Timeout in milliseconds.
     * @return Result with response envelope on success, error on failure.
     */
    Result<MessageEnvelope, std::string> sendBinaryAndReceive(
        const MessageEnvelope& envelope, int timeoutMs = 5000) override;

    // =========================================================================
    // Callbacks for async/unsolicited messages.
    // =========================================================================

    void onMessage(MessageCallback callback) { messageCallback_ = callback; }
    void onBinary(BinaryCallback callback) override { binaryCallback_ = callback; }
    void onServerCommand(ServerCommandCallback callback) override
    {
        serverCommandCallback_ = callback;
    }

    void setClientHello(const ClientHello& hello) override { clientHello_ = hello; }

    void setAccessToken(std::string token)
    {
        std::lock_guard<std::mutex> lock(accessTokenMutex_);
        accessToken_ = std::move(token);
    }

    void clearAccessToken()
    {
        std::lock_guard<std::mutex> lock(accessTokenMutex_);
        accessToken_.clear();
    }

    bool clientWantsEvents(const std::string& connectionId) const;
    bool clientWantsRender(const std::string& connectionId) const;
    void onConnected(ConnectionCallback callback) override { connectedCallback_ = callback; }
    void onDisconnected(ConnectionCallback callback) override { disconnectedCallback_ = callback; }
    void onError(ErrorCallback callback) override { errorCallback_ = callback; }

    /**
     * @brief Set callback for server-side client disconnect notifications.
     *
     * Called when a client disconnects from the server, providing the connection ID
     * so external code (e.g., StateMachine) can clean up associated state.
     */
    void onClientDisconnect(ClientDisconnectCallback callback)
    {
        clientDisconnectCallback_ = callback;
    }

    /**
     * @brief Set JSON deserializer for server-side JSON protocol support.
     *
     * Allows server/UI to inject their command deserializer without coupling
     * WebSocketService to specific command types.
     */
    void setJsonDeserializer(JsonDeserializer deserializer) override
    {
        jsonDeserializer_ = deserializer;
    }

    /**
     * @brief Set JSON command dispatcher for handling deserialized commands.
     *
     * The dispatcher receives the deserialized ApiCommand variant and is responsible
     * for visiting it, creating appropriate CWCs, and calling handlers.
     */
    void setJsonCommandDispatcher(JsonCommandDispatcher dispatcher) override
    {
        jsonDispatcher_ = dispatcher;
    }

    // =========================================================================
    // Server-side methods (listening for connections).
    // =========================================================================

    /**
     * @brief Start listening for incoming WebSocket connections.
     *
     * @param port Port to listen on.
     * @return Result with success or error message.
     */
    Result<std::monostate, std::string> listen(
        uint16_t port, const std::string& bindAddress = "0.0.0.0") override;

    /**
     * @brief Stop listening for connections.
     */
    void stopListening() override;
    void stopListening(bool disconnectClients);

    /**
     * @brief Check if server is currently listening.
     */
    bool isListening() const override;

    void closeNonLocalClients();

    void broadcastBinary(const std::vector<std::byte>& data);

    void broadcastRenderMessage(
        const WorldData& data, const std::vector<OrganismId>& organism_grid);

    void setClientRenderFormat(std::shared_ptr<rtc::WebSocket> ws, RenderFormat::EnumType format);

    RenderFormat::EnumType getClientRenderFormat(std::shared_ptr<rtc::WebSocket> ws) const;

    std::shared_ptr<rtc::WebSocket> getClientByConnectionId(const std::string& connectionId);

    /**
     * @brief Send a text message to a specific client by connection ID.
     *
     * Used for sending follow-up messages after the initial command response,
     * such as WebRTC ICE candidates.
     *
     * @param connectionId The connection identifier (from CWC.command.connectionId).
     * @param message The text message to send.
     * @return Result indicating success or error.
     */
    Result<std::monostate, std::string> sendToClient(
        const std::string& connectionId, const std::string& message) override;

    Result<std::monostate, std::string> sendToClient(
        const std::string& connectionId, const std::vector<std::byte>& data);

    /**
     * @brief Get the connection ID for a WebSocket.
     * Creates a new ID if this is a new connection.
     */
    std::string getConnectionId(std::shared_ptr<rtc::WebSocket> ws);

    /**
     * @brief Register a typed command handler (server-side).
     *
     * Handler receives CommandWithCallback and calls sendResponse() when done.
     * Supports both immediate (synchronous) and queued (asynchronous) handlers.
     *
     * @tparam CwcT The CommandWithCallback type (e.g., Api::StateGet::Cwc).
     * @param handler Function that receives CWC and eventually calls sendResponse().
     *
     * Example:
     *   service.registerHandler<Api::StateGet::Cwc>([](Api::StateGet::Cwc cwc) {
     *       // Immediate response
     *       cwc.sendResponse(Api::StateGet::Response::okay(getState()));
     *   });
     *
     *   service.registerHandler<Api::SimRun::Cwc>([sm](Api::SimRun::Cwc cwc) {
     *       // Queue to state machine, respond later
     *       sm->queueEvent(cwc);  // State machine calls sendResponse() when done
     *   });
     */
    template <typename CwcT>
    void registerHandler(std::function<void(CwcT)> handler)
    {
        using CommandT = typename CwcT::Command;
        using ResponseT = typename CwcT::Response;

        std::string cmdName(CommandT::name());
        spdlog::debug("WebSocketService: Registering handler for '{}'", cmdName);

        // Wrap typed handler in generic handler that handles serialization.
        commandHandlers_[cmdName] = [this, handler, cmdName](
                                        const std::vector<std::byte>& payload,
                                        std::shared_ptr<rtc::WebSocket> ws,
                                        uint64_t correlationId) {
            // Deserialize payload → typed command.
            // Value-initialize to silence GCC 13's -Wmaybe-uninitialized false positive.
            CommandT cmd{};
            try {
                cmd = Network::deserialize_payload<CommandT>(payload);
            }
            catch (const std::exception& e) {
                spdlog::error("Failed to deserialize {}: {}", cmdName, e.what());
                // TODO: Send error response.
                return;
            }

            // Build CWC with callback that sends response in appropriate format.
            CwcT cwc;
            cwc.command = cmd;

            // Populate connectionId if the Command type has that field.
            if constexpr (requires { cwc.command.connectionId; }) {
                cwc.command.connectionId = getConnectionId(ws);
            }

            cwc.callback = [this, ws, correlationId, cmdName](ResponseT&& response) {
                // Check which protocol this client is using.
                auto protocolIt = clientProtocols_.find(ws);
                Protocol clientProtocol =
                    (protocolIt != clientProtocols_.end()) ? protocolIt->second : Protocol::BINARY;

                if (clientProtocol == Protocol::JSON) {
                    nlohmann::json jsonResponse = makeJsonResponse(correlationId, response);
                    std::string jsonText = jsonResponse.dump();
                    spdlog::debug(
                        "WebSocketService: Sending {} JSON response ({} bytes)",
                        cmdName,
                        jsonText.size());
                    if (!ws || !ws->isOpen()) {
                        spdlog::error(
                            "WebSocketService: {} JSON response failed (socket closed)", cmdName);
                        return;
                    }
                    try {
                        ws->send(jsonText);
                    }
                    catch (const std::exception& e) {
                        spdlog::error(
                            "WebSocketService: {} JSON response send failed: {}",
                            cmdName,
                            e.what());
                    }
                }
                else {
                    // Send binary response.
                    auto envelope = Network::make_response_envelope(
                        correlationId, std::string(cmdName), response);
                    auto bytes = Network::serialize_envelope(envelope);
                    rtc::binary binaryMsg(bytes.begin(), bytes.end());
                    spdlog::debug(
                        "WebSocketService: Sending {} binary response ({} bytes)",
                        cmdName,
                        bytes.size());
                    if (!ws || !ws->isOpen()) {
                        spdlog::error(
                            "WebSocketService: {} binary response failed (socket closed)", cmdName);
                        return;
                    }
                    try {
                        ws->send(binaryMsg);
                    }
                    catch (const std::exception& e) {
                        spdlog::error(
                            "WebSocketService: {} binary response send failed: {}",
                            cmdName,
                            e.what());
                    }
                }
            };

            // Call handler - it will call cwc.sendResponse() or cwc.callback() when ready.
            handler(std::move(cwc));
        };
    }

    // =========================================================================
    // Instrumentation.
    // =========================================================================

    Timers& getTimers() { return timers_; }

private:
    // Protocol-specific command implementations.
    template <ApiCommandType CommandT>
    Result<typename CommandT::OkayType, std::string> sendCommandBinary(
        const CommandT& cmd, int timeoutMs);

    template <ApiCommandType CommandT>
    Result<typename CommandT::OkayType, std::string> sendCommandJson(
        const CommandT& cmd, int timeoutMs);

    Result<std::monostate, std::string> sendBinaryToDefaultPeer(const std::vector<std::byte>& data);

    // WebSocket connection.
    std::shared_ptr<rtc::WebSocket> ws_;
    std::string url_;
    Protocol protocol_ = Protocol::BINARY;

    // Connection state.
    std::atomic<bool> connectionFailed_{ false };

    // Pending requests with correlation IDs.
    struct PendingRequest {
        std::variant<std::string, std::vector<std::byte>> response;
        bool received = false;
        bool isBinary = false;
        std::mutex mutex;
        std::condition_variable cv;
    };
    std::atomic<uint64_t> nextId_{ 1 };
    std::map<uint64_t, std::shared_ptr<PendingRequest>> pendingRequests_;
    std::mutex pendingMutex_;

    // Callbacks.
    MessageCallback messageCallback_;
    BinaryCallback binaryCallback_;
    ConnectionCallback connectedCallback_;
    ConnectionCallback disconnectedCallback_;
    ErrorCallback errorCallback_;
    ClientDisconnectCallback clientDisconnectCallback_;
    ServerCommandCallback serverCommandCallback_;

    // =========================================================================
    // Server-side state (listening for connections).
    // =========================================================================

    using CommandHandler = std::function<void(
        const std::vector<std::byte>& payload,
        std::shared_ptr<rtc::WebSocket> ws,
        uint64_t correlationId)>;

    std::unique_ptr<rtc::WebSocketServer> server_;
    std::map<std::string, CommandHandler> commandHandlers_;
    std::vector<std::shared_ptr<rtc::WebSocket>> connectedClients_;
    std::map<std::shared_ptr<rtc::WebSocket>, Protocol> clientProtocols_;
    std::map<std::shared_ptr<rtc::WebSocket>, RenderFormat::EnumType> clientRenderFormats_;
    std::map<std::shared_ptr<rtc::WebSocket>, ClientHello> clientHellos_;
    mutable std::mutex clientsMutex_;

    // Connection ID registry for directed messaging.
    std::atomic<uint64_t> nextConnectionId_{ 1 };
    std::map<std::string, std::weak_ptr<rtc::WebSocket>> connectionRegistry_; // ID -> connection.
    std::map<std::shared_ptr<rtc::WebSocket>, std::string> connectionIds_;    // connection -> ID.

    ClientHello clientHello_{};

    JsonDeserializer jsonDeserializer_;    // Injected JSON deserializer (server/UI provides).
    JsonCommandDispatcher jsonDispatcher_; // Injected JSON command dispatcher (server/UI provides).

    std::string accessToken_;
    mutable std::mutex accessTokenMutex_;

    void onClientConnected(std::shared_ptr<rtc::WebSocket> ws);
    void onClientMessage(std::shared_ptr<rtc::WebSocket> ws, const rtc::binary& data);
    void onClientMessageJson(std::shared_ptr<rtc::WebSocket> ws, const std::string& jsonText);

    // Instrumentation.
    Timers timers_;
};

// =============================================================================
// Template implementations.
// =============================================================================

template <ApiCommandType CommandT>
Result<typename CommandT::OkayType, std::string> WebSocketService::sendCommandBinary(
    const CommandT& cmd, int timeoutMs)
{
    // Build command envelope.
    uint64_t id = nextId_.fetch_add(1);
    auto envelope = make_command_envelope(id, cmd);

    // Send and receive.
    auto result = sendBinaryAndReceive(envelope, timeoutMs);
    if (result.isError()) {
        return Result<typename CommandT::OkayType, std::string>::error(result.errorValue());
    }

    const MessageEnvelope& responseEnvelope = result.value();

    // Verify response type.
    std::string expectedType = std::string(CommandT::name()) + "_response";
    if (responseEnvelope.message_type != expectedType) {
        return Result<typename CommandT::OkayType, std::string>::error(
            "Unexpected response type: " + responseEnvelope.message_type + " (expected "
            + expectedType + ")");
    }

    // Extract result from envelope.
    try {
        auto extractedResult =
            extract_result<typename CommandT::OkayType, ApiError>(responseEnvelope);
        if (extractedResult.isError()) {
            return Result<typename CommandT::OkayType, std::string>::error(
                extractedResult.errorValue().message);
        }
        return Result<typename CommandT::OkayType, std::string>::okay(extractedResult.value());
    }
    catch (const std::exception& e) {
        return Result<typename CommandT::OkayType, std::string>::error(
            std::string("Failed to extract result: ") + e.what());
    }
}

template <ApiCommandType CommandT>
Result<typename CommandT::OkayType, std::string> WebSocketService::sendCommandJson(
    const CommandT& cmd, int timeoutMs)
{
    // Build JSON message.
    nlohmann::json json = cmd.toJson();
    json["command"] = std::string(CommandT::name());

    // Send and receive.
    auto result = sendJsonAndReceive(json.dump(), timeoutMs);
    if (result.isError()) {
        return Result<typename CommandT::OkayType, std::string>::error(result.errorValue().message);
    }

    // Parse response.
    nlohmann::json responseJson;
    try {
        responseJson = nlohmann::json::parse(result.value());
    }
    catch (const std::exception& e) {
        return Result<typename CommandT::OkayType, std::string>::error(
            std::string("Invalid JSON response: ") + e.what());
    }

    // Check for error.
    if (responseJson.contains("error")) {
        std::string errorMsg = "Unknown error";
        if (responseJson["error"].is_string()) {
            errorMsg = responseJson["error"].get<std::string>();
        }
        else if (responseJson["error"].is_object() && responseJson["error"].contains("message")) {
            errorMsg = responseJson["error"]["message"].get<std::string>();
        }
        return Result<typename CommandT::OkayType, std::string>::error(errorMsg);
    }

    // Extract value.
    if (!responseJson.contains("value")) {
        return Result<typename CommandT::OkayType, std::string>::error(
            "Response missing 'value' field");
    }

    try {
        if constexpr (std::is_same_v<typename CommandT::OkayType, std::monostate>) {
            return Result<typename CommandT::OkayType, std::string>::okay(std::monostate{});
        }
        else {
            auto okay = CommandT::OkayType::fromJson(responseJson["value"]);
            return Result<typename CommandT::OkayType, std::string>::okay(std::move(okay));
        }
    }
    catch (const std::exception& e) {
        return Result<typename CommandT::OkayType, std::string>::error(
            std::string("Failed to deserialize response: ") + e.what());
    }
}

} // namespace Network
} // namespace DirtSim
