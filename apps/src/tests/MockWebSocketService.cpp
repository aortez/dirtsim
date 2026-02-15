#include "tests/MockWebSocketService.h"
#include <gtest/gtest.h>

namespace DirtSim::Tests {

Result<std::monostate, std::string> MockWebSocketService::connect(
    const std::string& /*url*/, int /*timeoutMs*/)
{
    connected_ = true;
    if (connectedCallback_) {
        connectedCallback_();
    }
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void MockWebSocketService::disconnect()
{
    const bool wasConnected = connected_;
    connected_ = false;
    if (wasConnected && disconnectedCallback_) {
        disconnectedCallback_();
    }
}

bool MockWebSocketService::isConnected() const
{
    return connected_;
}

std::string MockWebSocketService::getUrl() const
{
    return "ws://mock:8080";
}

Result<std::monostate, std::string> MockWebSocketService::listen(
    uint16_t /*port*/, const std::string& /*bindAddress*/)
{
    listening_ = true;
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

bool MockWebSocketService::isListening() const
{
    return listening_;
}

void MockWebSocketService::stopListening()
{
    listening_ = false;
}

void MockWebSocketService::stopListening(bool /*disconnectClients*/)
{
    listening_ = false;
}

Result<std::monostate, std::string> MockWebSocketService::sendBinary(
    const std::vector<std::byte>& /*data*/)
{
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> MockWebSocketService::sendToClient(
    const std::string& /*connectionId*/, const std::string& /*message*/)
{
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> MockWebSocketService::sendToClient(
    const std::string& /*connectionId*/, const std::vector<std::byte>& /*data*/)
{
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void MockWebSocketService::setAccessToken(std::string token)
{
    accessToken_ = token;
}

void MockWebSocketService::clearAccessToken()
{
    accessToken_.clear();
}

void MockWebSocketService::closeNonLocalClients()
{}

bool MockWebSocketService::clientWantsEvents(const std::string& /*connectionId*/) const
{
    return true;
}

bool MockWebSocketService::clientWantsRender(const std::string& /*connectionId*/) const
{
    return true;
}

Result<Network::MessageEnvelope, std::string> MockWebSocketService::sendBinaryAndReceive(
    const Network::MessageEnvelope& envelope, int /*timeoutMs*/)
{
    sentCommands_.push_back(envelope.message_type);

    auto it = responses_.find(envelope.message_type);
    if (it != responses_.end()) {
        auto response = it->second;
        response.id = envelope.id;
        return Result<Network::MessageEnvelope, std::string>::okay(response);
    }

    ADD_FAILURE() << "MockWebSocketService: No response configured for: " << envelope.message_type;
    return Result<Network::MessageEnvelope, std::string>::error(
        "No response configured for: " + envelope.message_type);
}

} // namespace DirtSim::Tests
