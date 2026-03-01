#pragma once

#include "BinaryProtocol.h"
#include "ClientHello.h"
#include "JsonProtocol.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include <any>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <rtc/rtc.hpp>
#include <string>
#include <vector>

namespace DirtSim {
namespace Network {

/**
 * @brief Interface for WebSocket service implementations.
 *
 * Allows dependency injection of mock implementations for testing.
 * Uses NVI pattern - template methods are non-virtual and call virtual hooks.
 */
class WebSocketServiceInterface {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using BinaryCallback = std::function<void(const std::vector<std::byte>&)>;
    using ConnectionCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using ServerCommandCallback =
        std::function<void(const std::string& messageType, const std::vector<std::byte>& payload)>;
    using JsonDeserializer = std::function<std::any(const std::string&)>;
    using CommandHandler = std::function<void(
        const std::vector<std::byte>& payload,
        std::shared_ptr<rtc::WebSocket> ws,
        uint64_t correlationId)>;

    virtual ~WebSocketServiceInterface() = default;

    virtual Result<std::monostate, std::string> connect(
        const std::string& url, int timeoutMs = 5000) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual std::string getUrl() const = 0;

    virtual Result<std::monostate, std::string> listen(
        uint16_t port, const std::string& bindAddress = "0.0.0.0") = 0;
    virtual bool isListening() const = 0;
    virtual void stopListening() = 0;
    virtual void stopListening(bool disconnectClients) = 0;

    virtual Result<std::monostate, std::string> sendBinary(const std::vector<std::byte>& data) = 0;
    virtual Result<std::monostate, std::string> sendToClient(
        const std::string& connectionId, const std::string& message) = 0;
    virtual Result<std::monostate, std::string> sendToClient(
        const std::string& connectionId, const std::vector<std::byte>& data) = 0;

    virtual void setAccessToken(std::string token) = 0;
    virtual void clearAccessToken() = 0;
    virtual void closeNonLocalClients() = 0;

    virtual bool clientWantsEvents(const std::string& connectionId) const = 0;
    virtual bool clientWantsRender(const std::string& connectionId) const = 0;

    virtual void onConnected(ConnectionCallback callback) = 0;
    virtual void onDisconnected(ConnectionCallback callback) = 0;
    virtual void onError(ErrorCallback callback) = 0;
    virtual void onBinary(BinaryCallback callback) = 0;
    virtual void onServerCommand(ServerCommandCallback callback) = 0;

    virtual void setClientHello(const ClientHello& /*hello*/) {}

    virtual void setJsonDeserializer(JsonDeserializer deserializer) = 0;

    virtual void registerCommandHandler(std::string commandName, CommandHandler handler) = 0;
    virtual std::string getConnectionId(std::shared_ptr<rtc::WebSocket> ws) = 0;
    virtual bool isJsonClient(std::shared_ptr<rtc::WebSocket> ws) const = 0;
    virtual void reportCommandHandlerDeserializeError(
        const std::string& commandName, const std::string& errorMessage) = 0;

    virtual uint64_t allocateRequestId()
    {
        static std::atomic<uint64_t> nextId{ 1 };
        return nextId.fetch_add(1, std::memory_order_relaxed);
    }

    // Template methods (non-virtual wrappers over virtual hooks).
    template <typename CwcType>
    void registerHandler(std::function<void(CwcType)> handler)
    {
        using CommandT = typename CwcType::Command;
        using ResponseT = typename CwcType::Response;

        std::string cmdName(CommandT::name());

        registerCommandHandler(
            cmdName,
            [this, handler = std::move(handler), cmdName](
                const std::vector<std::byte>& payload,
                std::shared_ptr<rtc::WebSocket> ws,
                uint64_t correlationId) {
                CommandT cmd{};
                try {
                    cmd = deserialize_payload<CommandT>(payload);
                }
                catch (const std::exception& e) {
                    reportCommandHandlerDeserializeError(cmdName, e.what());

                    const ApiError error("Failed to deserialize command '" + cmdName + "'");
                    if constexpr (requires { ResponseT::error(error); }) {
                        auto response = ResponseT::error(error);
                        if (!ws || !ws->isOpen()) {
                            return;
                        }
                        if (isJsonClient(ws)) {
                            nlohmann::json jsonResponse = makeJsonResponse(correlationId, response);
                            const std::string jsonText = jsonResponse.dump();
                            try {
                                ws->send(jsonText);
                            }
                            catch (const std::exception&) {
                                return;
                            }
                        }
                        else {
                            auto envelope =
                                make_response_envelope(correlationId, cmdName, response);
                            const auto bytes = serialize_envelope(envelope);
                            rtc::binary binaryMsg(bytes.begin(), bytes.end());
                            try {
                                ws->send(binaryMsg);
                            }
                            catch (const std::exception&) {
                                return;
                            }
                        }
                    }
                    return;
                }

                CwcType cwc{};
                cwc.command = cmd;
                cwc.usesBinary = !isJsonClient(ws);

                if constexpr (requires { cwc.command.connectionId; }) {
                    cwc.command.connectionId = getConnectionId(ws);
                }

                cwc.callback = [this, ws, correlationId, cmdName](ResponseT&& response) {
                    if (isJsonClient(ws)) {
                        nlohmann::json jsonResponse = makeJsonResponse(correlationId, response);
                        const std::string jsonText = jsonResponse.dump();
                        if (!ws || !ws->isOpen()) {
                            return;
                        }
                        try {
                            ws->send(jsonText);
                        }
                        catch (const std::exception&) {
                            return;
                        }
                    }
                    else {
                        auto envelope = make_response_envelope(correlationId, cmdName, response);
                        const auto bytes = serialize_envelope(envelope);
                        rtc::binary binaryMsg(bytes.begin(), bytes.end());
                        if (!ws || !ws->isOpen()) {
                            return;
                        }
                        try {
                            ws->send(binaryMsg);
                        }
                        catch (const std::exception&) {
                            return;
                        }
                    }
                };

                handler(std::move(cwc));
            });
    }

    using HandlerInvoker = std::function<void(
        std::string commandName, std::vector<std::byte> payload, uint64_t correlationId)>;
    using JsonCommandDispatcher = std::function<void(
        std::any cmdVariant,
        std::shared_ptr<rtc::WebSocket> ws,
        uint64_t correlationId,
        HandlerInvoker invokeHandler)>;

    virtual void setJsonCommandDispatcher(JsonCommandDispatcher /*dispatcher*/)
    {
        // No-op for mocks - real implementation in WebSocketService.
    }

    template <typename Command>
    Result<std::monostate, std::string> sendCommand(const Command& cmd)
    {
        auto envelope = make_command_envelope(0, cmd);
        return sendBinary(serialize_envelope(envelope));
    }

    template <typename Okay, typename Command>
    Result<Result<Okay, ApiError>, std::string> sendCommandAndGetResponse(
        const Command& cmd, int timeoutMs = 5000)
    {
        // Generate unique request ID.
        uint64_t requestId = allocateRequestId();

        // Create envelope.
        auto envelope = make_command_envelope(requestId, cmd);

        // Send and receive via virtual method.
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

    virtual Result<MessageEnvelope, std::string> sendBinaryAndReceive(
        const MessageEnvelope& envelope, int timeoutMs = 5000) = 0;
};

} // namespace Network
} // namespace DirtSim
