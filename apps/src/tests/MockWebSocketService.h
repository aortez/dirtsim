#pragma once

#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketServiceInterface.h"
#include "server/api/ApiError.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace DirtSim::Tests {

class MockWebSocketService : public Network::WebSocketServiceInterface {
public:
    MockWebSocketService() = default;

    template <typename CommandType>
    void expectSuccess(const typename CommandType::OkayType& okay)
    {
        auto response = Result<typename CommandType::OkayType, ApiError>::okay(okay);
        responses_[std::string(CommandType::name())] =
            Network::make_response_envelope(0, std::string(CommandType::name()), response);
    }

    template <typename CommandType>
    void expectError(const std::string& message)
    {
        ApiError error;
        error.message = message;
        auto response = Result<typename CommandType::OkayType, ApiError>::error(error);
        responses_[std::string(CommandType::name())] =
            Network::make_response_envelope(0, std::string(CommandType::name()), response);
    }

    const std::vector<std::string>& sentCommands() const { return sentCommands_; }
    void clearSentCommands() { sentCommands_.clear(); }

    Result<std::monostate, std::string> connect(
        const std::string& /*url*/, int /*timeoutMs*/ = 5000) override;
    void disconnect() override;
    bool isConnected() const override;
    std::string getUrl() const override;

    Result<std::monostate, std::string> listen(
        uint16_t /*port*/, const std::string& /*bindAddress*/ = "0.0.0.0") override;
    bool isListening() const override;
    void stopListening() override;
    void stopListening(bool disconnectClients) override;

    Result<std::monostate, std::string> sendBinary(const std::vector<std::byte>& /*data*/) override;
    Result<std::monostate, std::string> sendToClient(
        const std::string& /*connectionId*/, const std::string& /*message*/) override;
    Result<std::monostate, std::string> sendToClient(
        const std::string& /*connectionId*/, const std::vector<std::byte>& /*data*/) override;

    void setAccessToken(std::string token) override;
    void clearAccessToken() override;
    void closeNonLocalClients() override;

    bool clientWantsEvents(const std::string& /*connectionId*/) const override;
    bool clientWantsRender(const std::string& /*connectionId*/) const override;

    void onConnected(ConnectionCallback callback) override
    {
        connectedCallback_ = std::move(callback);
    }
    void onDisconnected(ConnectionCallback callback) override
    {
        disconnectedCallback_ = std::move(callback);
    }
    void onError(ErrorCallback callback) override { errorCallback_ = std::move(callback); }
    void onBinary(BinaryCallback /*callback*/) override {}
    void onServerCommand(ServerCommandCallback /*callback*/) override {}
    void setJsonDeserializer(JsonDeserializer /*deserializer*/) override {}
    void registerCommandHandler(std::string /*commandName*/, CommandHandler /*handler*/) override {}
    std::string getConnectionId(std::shared_ptr<rtc::WebSocket> /*ws*/) override { return ""; }
    bool isJsonClient(std::shared_ptr<rtc::WebSocket> /*ws*/) const override { return false; }

    Result<Network::MessageEnvelope, std::string> sendBinaryAndReceive(
        const Network::MessageEnvelope& envelope, int /*timeoutMs*/ = 5000) override;

private:
    bool connected_ = true;
    bool listening_ = false;
    std::string accessToken_;
    std::map<std::string, Network::MessageEnvelope> responses_;
    std::vector<std::string> sentCommands_;
    ConnectionCallback connectedCallback_;
    ConnectionCallback disconnectedCallback_;
    ErrorCallback errorCallback_;
};

} // namespace DirtSim::Tests
