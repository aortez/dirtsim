#include "core/CommandWithCallback.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/ApiError.h"
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <zpp_bits.h>

using namespace DirtSim;
using namespace DirtSim::Network;

namespace {
uint16_t allocateFreePort()
{
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 0;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(0);

    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        ::close(sock);
        return 0;
    }

    const uint16_t port = ntohs(addr.sin_port);
    ::close(sock);
    return port;
}

struct PingCommand {
    int value = 0;

    static constexpr const char* name() { return "PingCommand"; }
    using serialize = zpp::bits::members<1>;
};

struct PingOkay {
    int value = 0;

    nlohmann::json toJson() const { return { { "value", value } }; }

    using serialize = zpp::bits::members<1>;
};

using PingResponse = Result<PingOkay, ApiError>;
using PingCwc = CommandWithCallback<PingCommand, PingResponse>;

struct PongCommand {
    int value = 0;

    static constexpr const char* name() { return "PongCommand"; }
    using serialize = zpp::bits::members<1>;
};

struct PongOkay {
    int value = 0;

    nlohmann::json toJson() const { return { { "value", value } }; }

    using serialize = zpp::bits::members<1>;
};

using PongResponse = Result<PongOkay, ApiError>;
using PongCwc = CommandWithCallback<PongCommand, PongResponse>;

struct MismatchCommand {
    int value = 0;

    static constexpr const char* name() { return "MismatchCommand"; }
    using serialize = zpp::bits::members<1>;
};

struct EmptyOkay {
    nlohmann::json toJson() const { return nlohmann::json::object(); }

    using serialize = zpp::bits::members<0>;
};

using MismatchResponse = Result<EmptyOkay, ApiError>;
using MismatchCwc = CommandWithCallback<MismatchCommand, MismatchResponse>;

struct PushPayload {
    int value = 0;

    using serialize = zpp::bits::members<1>;
};

struct JsonPingCommand {
    int value = 0;
};
} // namespace

TEST(WebSocketServiceIntegrationTest, ClientToServerRequestResponse)
{
    const uint16_t port = allocateFreePort();
    ASSERT_NE(port, 0);

    WebSocketService server;
    ASSERT_TRUE(server.listen(port).isValue());
    server.registerHandler<PingCwc>([](PingCwc cwc) {
        PingOkay okay{ .value = cwc.command.value + 1 };
        cwc.sendResponse(PingResponse::okay(okay));
    });

    WebSocketService client;
    auto connectResult = client.connect("ws://localhost:" + std::to_string(port), 2000);
    ASSERT_TRUE(connectResult.isValue());

    PingCommand cmd{ .value = 41 };
    auto result = client.sendCommandAndGetResponse<PingOkay>(cmd, 2000);
    ASSERT_TRUE(result.isValue());
    ASSERT_TRUE(result.value().isValue());
    EXPECT_EQ(result.value().value().value, 42);

    client.disconnect();
    server.stopListening();
}

TEST(WebSocketServiceIntegrationTest, ServerToClientRequestResponse)
{
    const uint16_t port = allocateFreePort();
    ASSERT_NE(port, 0);

    WebSocketService server;
    ASSERT_TRUE(server.listen(port).isValue());
    server.registerHandler<PingCwc>([](PingCwc cwc) {
        PingOkay okay{ .value = cwc.command.value };
        cwc.sendResponse(PingResponse::okay(okay));
    });

    WebSocketService client;
    client.registerHandler<PongCwc>([](PongCwc cwc) {
        PongOkay okay{ .value = cwc.command.value + 1 };
        cwc.sendResponse(PongResponse::okay(okay));
    });
    ClientHello hello{
        .protocolVersion = kClientHelloProtocolVersion,
        .wantsEvents = true,
    };
    client.setClientHello(hello);

    auto connectResult = client.connect("ws://localhost:" + std::to_string(port), 2000);
    ASSERT_TRUE(connectResult.isValue());

    PingCommand warmup{ .value = 1 };
    auto warmupResult = client.sendCommandAndGetResponse<PingOkay>(warmup, 2000);
    ASSERT_TRUE(warmupResult.isValue());
    ASSERT_TRUE(warmupResult.value().isValue());

    PongCommand cmd{ .value = 5 };
    auto response = server.sendCommandAndGetResponse<PongOkay>(cmd, 2000);
    ASSERT_TRUE(response.isValue());
    ASSERT_TRUE(response.value().isValue());
    EXPECT_EQ(response.value().value().value, 6);

    client.disconnect();
    server.stopListening();
}

TEST(WebSocketServiceIntegrationTest, DeserializationMismatchReturnsErrorInsteadOfThrowing)
{
    const uint16_t port = allocateFreePort();
    ASSERT_NE(port, 0);

    WebSocketService server;
    ASSERT_TRUE(server.listen(port).isValue());
    server.registerHandler<MismatchCwc>(
        [](MismatchCwc cwc) { cwc.sendResponse(MismatchResponse::okay(EmptyOkay{})); });

    WebSocketService client;
    auto connectResult = client.connect("ws://localhost:" + std::to_string(port), 2000);
    ASSERT_TRUE(connectResult.isValue());

    MismatchCommand cmd{ .value = 7 };
    const auto response = client.sendCommandAndGetResponse<PingOkay>(cmd, 2000);
    ASSERT_TRUE(response.isError());
    EXPECT_NE(response.errorValue().find("Failed to deserialize response"), std::string::npos);

    client.disconnect();
    server.stopListening();
}

TEST(WebSocketServiceIntegrationTest, PushPathUnchanged)
{
    const uint16_t port = allocateFreePort();
    ASSERT_NE(port, 0);

    WebSocketService server;
    ASSERT_TRUE(server.listen(port).isValue());
    server.registerHandler<PingCwc>([](PingCwc cwc) {
        PingOkay okay{ .value = cwc.command.value };
        cwc.sendResponse(PingResponse::okay(okay));
    });

    WebSocketService client;
    ClientHello hello{
        .protocolVersion = kClientHelloProtocolVersion,
        .wantsEvents = true,
    };
    client.setClientHello(hello);
    auto connectResult = client.connect("ws://localhost:" + std::to_string(port), 2000);
    ASSERT_TRUE(connectResult.isValue());

    std::mutex mutex;
    std::condition_variable cv;
    bool received = false;
    std::string receivedType;
    std::vector<std::byte> receivedPayload;

    client.onServerCommand(
        [&](const std::string& messageType, const std::vector<std::byte>& payload) {
            std::lock_guard<std::mutex> lock(mutex);
            received = true;
            receivedType = messageType;
            receivedPayload = payload;
            cv.notify_one();
        });

    PingCommand warmup{ .value = 1 };
    auto warmupResult = client.sendCommandAndGetResponse<PingOkay>(warmup, 2000);
    ASSERT_TRUE(warmupResult.isValue());
    ASSERT_TRUE(warmupResult.value().isValue());

    PushPayload payload{ .value = 7 };
    MessageEnvelope envelope{
        .id = 0,
        .message_type = "TestPush",
        .payload = serialize_payload(payload),
    };
    server.broadcastBinary(serialize_envelope(envelope));

    std::unique_lock<std::mutex> lock(mutex);
    bool callbackFired =
        cv.wait_for(lock, std::chrono::milliseconds(1000), [&]() { return received; });

    ASSERT_TRUE(callbackFired);
    EXPECT_EQ(receivedType, "TestPush");
    auto decoded = deserialize_payload<PushPayload>(receivedPayload);
    EXPECT_EQ(decoded.value, 7);

    client.disconnect();
    server.stopListening();
}

TEST(WebSocketServiceIntegrationTest, ServerToClientTimeout)
{
    const uint16_t port = allocateFreePort();
    ASSERT_NE(port, 0);

    WebSocketService server;
    ASSERT_TRUE(server.listen(port).isValue());
    server.registerHandler<PingCwc>([](PingCwc cwc) {
        PingOkay okay{ .value = cwc.command.value };
        cwc.sendResponse(PingResponse::okay(okay));
    });

    WebSocketService client;
    ClientHello hello{
        .protocolVersion = kClientHelloProtocolVersion,
        .wantsEvents = true,
    };
    client.setClientHello(hello);
    auto connectResult = client.connect("ws://localhost:" + std::to_string(port), 2000);
    ASSERT_TRUE(connectResult.isValue());

    PingCommand warmup{ .value = 1 };
    auto warmupResult = client.sendCommandAndGetResponse<PingOkay>(warmup, 2000);
    ASSERT_TRUE(warmupResult.isValue());
    ASSERT_TRUE(warmupResult.value().isValue());

    PongCommand cmd{ .value = 5 };
    auto response = server.sendCommandAndGetResponse<PongOkay>(cmd, 200);
    ASSERT_TRUE(response.isError());
    EXPECT_EQ(response.errorValue(), "Response timeout");

    client.disconnect();
    server.stopListening();
}

TEST(WebSocketServiceIntegrationTest, JsonRequestResponse)
{
    const uint16_t port = allocateFreePort();
    ASSERT_NE(port, 0);

    WebSocketService server;
    ASSERT_TRUE(server.listen(port).isValue());
    server.setJsonDeserializer([](const std::string& jsonText) -> std::any {
        auto json = nlohmann::json::parse(jsonText);
        if (!json.contains("command") || json["command"] != "JsonPing") {
            throw std::runtime_error("Unknown command");
        }

        JsonPingCommand cmd{ .value = json.value("value", 0) };
        return cmd;
    });

    server.setJsonCommandDispatcher([](std::any cmdAny,
                                       std::shared_ptr<rtc::WebSocket> ws,
                                       uint64_t correlationId,
                                       WebSocketService::HandlerInvoker /*invokeHandler*/) {
        const auto cmd = std::any_cast<JsonPingCommand>(cmdAny);
        nlohmann::json response = {
            { "id", correlationId },
            { "success", true },
            { "value", { { "value", cmd.value + 1 } } },
        };
        ws->send(response.dump());
    });

    WebSocketService client;
    client.setProtocol(Protocol::JSON);
    auto connectResult = client.connect("ws://localhost:" + std::to_string(port), 2000);
    ASSERT_TRUE(connectResult.isValue());

    nlohmann::json request = { { "command", "JsonPing" }, { "value", 41 } };
    auto response = client.sendJsonAndReceive(request.dump(), 2000);
    ASSERT_TRUE(response.isValue());

    auto responseJson = nlohmann::json::parse(response.value());
    EXPECT_EQ(responseJson["value"]["value"], 42);

    client.disconnect();
    server.stopListening();
}
