#pragma once

#include "BinaryProtocol.h"
#include "ClientHello.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include <any>
#include <atomic>
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

    virtual uint64_t allocateRequestId()
    {
        static std::atomic<uint64_t> nextId{ 1 };
        return nextId.fetch_add(1, std::memory_order_relaxed);
    }

    // Template methods (non-virtual, no-ops in base for mocking).
    template <typename CwcType>
    void registerHandler(std::function<void(CwcType)> /*handler*/)
    {
        // No-op for mocks - real implementation in WebSocketService.
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
