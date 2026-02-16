#include "WebSocketService.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/RenderMessageUtils.h"
#include "core/WorldData.h"
#include "core/network/JsonProtocol.h"
#include "server/api/ApiCommand.h"
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>
#include <string_view>
#include <thread>
#include <zpp_bits.h>

namespace DirtSim {
namespace Network {

namespace {
constexpr const char* kClientHelloMessageType = "ClientHello";
constexpr auto kAuthAcceptDelay = std::chrono::milliseconds(100);
constexpr auto kAuthRejectDelay = std::chrono::milliseconds(500);

bool isUiHello(const ClientHello& hello)
{
    return hello.wantsRender;
}

bool isResponseMessageType(const std::string& messageType)
{
    constexpr std::string_view suffix = "_response";
    if (messageType.size() < suffix.size()) {
        return false;
    }
    return messageType.compare(messageType.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string extractHostFromRemoteAddress(const std::string& remoteAddress)
{
    const auto pos = remoteAddress.rfind(':');
    if (pos == std::string::npos) {
        return remoteAddress;
    }
    return remoteAddress.substr(0, pos);
}

bool isLoopbackHost(const std::string& host)
{
    if (host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "0:0:0:0:0:0:0:1") {
        return true;
    }
    if (host.rfind("127.", 0) == 0) {
        return true;
    }
    if (host.rfind("::ffff:127.", 0) == 0) {
        return true;
    }
    return false;
}

std::string extractTokenFromPath(const std::optional<std::string>& path)
{
    if (!path.has_value()) {
        return "";
    }

    const auto queryPos = path->find('?');
    if (queryPos == std::string::npos) {
        return "";
    }

    const std::string query = path->substr(queryPos + 1);
    size_t pos = 0;
    while (pos < query.size()) {
        const auto ampPos = query.find('&', pos);
        const auto partLen = ampPos == std::string::npos ? std::string::npos : ampPos - pos;
        const std::string part = query.substr(pos, partLen);
        const auto eqPos = part.find('=');
        if (eqPos != std::string::npos) {
            const std::string key = part.substr(0, eqPos);
            if (key == "token") {
                return part.substr(eqPos + 1);
            }
        }
        if (ampPos == std::string::npos) {
            break;
        }
        pos = ampPos + 1;
    }

    return "";
}
} // namespace

WebSocketService::WebSocketService()
{
    LOG_DEBUG(Network, "WebSocketService created");
}

WebSocketService::~WebSocketService()
{
    disconnect();
    stopListening();
}

Result<std::monostate, std::string> WebSocketService::connect(const std::string& url, int timeoutMs)
{
    try {
        LOG_INFO(Network, "Connecting to {}", url);

        // Reset connection state.
        connectionFailed_ = false;
        helloSent_ = false;
        url_ = url;

        // Create WebSocket with large message size.
        // Default is 256 KB which is too small for RenderMessage on large worlds.
        // 200x200 world with DebugCell (~23 bytes/cell) = ~920 KB.
        rtc::WebSocketConfiguration config;
        config.maxMessageSize = 16 * 1024 * 1024; // 16 MB.

        ws_ = std::make_shared<rtc::WebSocket>(config);

        // Set up message handler.
        ws_->onMessage([this](std::variant<rtc::binary, rtc::string> data) {
            if (std::holds_alternative<rtc::string>(data)) {
                // JSON text message.
                std::string message = std::get<rtc::string>(data);
                LOG_DEBUG(Network, "Received text ({} bytes)", message.size());

                // Extract correlation ID.
                std::optional<uint64_t> correlationId;
                try {
                    nlohmann::json json = nlohmann::json::parse(message);
                    if (json.contains("id") && json["id"].is_number()) {
                        correlationId = json["id"].get<uint64_t>();
                    }
                }
                catch (...) {
                    DIRTSIM_ASSERT(false, "Failed to parse JSON message");
                }

                if (correlationId.has_value()) {
                    // Route to pending request.
                    std::lock_guard<std::mutex> lock(pendingMutex_);
                    auto it = pendingRequests_.find(*correlationId);
                    if (it != pendingRequests_.end()) {
                        auto& pending = it->second;
                        std::lock_guard<std::mutex> reqLock(pending->mutex);
                        pending->response = message;
                        pending->isBinary = false;
                        pending->received = true;
                        pending->cv.notify_one();
                    }
                }
                else if (messageCallback_) {
                    messageCallback_(message);
                }
            }
            else {
                // Binary message.
                const auto& binaryData = std::get<rtc::binary>(data);
                LOG_DEBUG(Network, "Received binary ({} bytes)", binaryData.size());

                // Convert to std::vector<std::byte>.
                std::vector<std::byte> bytes(binaryData.size());
                std::memcpy(bytes.data(), binaryData.data(), binaryData.size());

                // Deserialize as MessageEnvelope (all messages use this wrapper now).
                try {
                    MessageEnvelope envelope = deserialize_envelope(bytes);

                    // Check if this is a server push (RenderMessage) or a command response.
                    if (envelope.message_type == "RenderMessage") {
                        // Server push - extract payload and route to binaryCallback_.
                        LOG_DEBUG(
                            Network,
                            "WebSocketService CLIENT: Received RenderMessage push ({} bytes "
                            "payload)",
                            envelope.payload.size());
                        if (binaryCallback_) {
                            binaryCallback_(envelope.payload);
                        }
                    }
                    else if (envelope.id > 0 && isResponseMessageType(envelope.message_type)) {
                        // Command response - route to pending request by correlation ID.
                        std::lock_guard<std::mutex> lock(pendingMutex_);
                        auto it = pendingRequests_.find(envelope.id);
                        if (it != pendingRequests_.end()) {
                            auto& pending = it->second;
                            std::lock_guard<std::mutex> reqLock(pending->mutex);
                            pending->response = bytes;
                            pending->isBinary = true;
                            pending->received = true;
                            pending->cv.notify_one();
                        }
                        else {
                            LOG_DEBUG(
                                Network,
                                "WebSocketService CLIENT: Response {} not found (already "
                                "processed)",
                                envelope.id);
                        }
                    }
                    else if (envelope.id == 0 && serverCommandCallback_) {
                        // Server-pushed command (no correlation ID).
                        LOG_DEBUG(
                            Network,
                            "WebSocketService CLIENT: Received server command '{}'",
                            envelope.message_type);
                        serverCommandCallback_(envelope.message_type, envelope.payload);
                    }
                    else if (envelope.id > 0) {
                        // Server-initiated command - route to registered handler.
                        auto it = commandHandlers_.find(envelope.message_type);
                        if (it != commandHandlers_.end()) {
                            it->second(envelope.payload, ws_, envelope.id);
                        }
                        else {
                            LOG_WARN(
                                Network,
                                "WebSocketService CLIENT: No handler for command '{}'",
                                envelope.message_type);
                        }
                    }
                }
                catch (const std::exception& e) {
                    LOG_ERROR(
                        Network,
                        "WebSocketService CLIENT: Failed to deserialize envelope: {}",
                        e.what());
                }
            }
        });

        // Set up open handler.
        ws_->onOpen([this]() {
            LOG_DEBUG(Network, "Connection opened");
            sendClientHelloIfNeeded();
            if (connectedCallback_) {
                connectedCallback_();
            }
        });

        // Set up close handler.
        ws_->onClosed([this]() {
            LOG_DEBUG(Network, "Connection closed");
            connectionFailed_ = true;
            if (disconnectedCallback_) {
                disconnectedCallback_();
            }
        });

        // Set up error handler.
        ws_->onError([this](std::string error) {
            LOG_ERROR(Network, "WebSocketService error: {}", error);
            connectionFailed_ = true;
            if (errorCallback_) {
                errorCallback_(error);
            }
        });

        // Open connection.
        ws_->open(url);

        if (timeoutMs <= 0) {
            LOG_INFO(Network, "Connection initiated to {} (async mode)", url);
            return Result<std::monostate, std::string>::okay(std::monostate{});
        }

        // Wait for connection with timeout.
        auto startTime = std::chrono::steady_clock::now();
        while (!ws_->isOpen() && !connectionFailed_) {
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                > timeoutMs) {
                return Result<std::monostate, std::string>::error("Connection timeout");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (connectionFailed_) {
            return Result<std::monostate, std::string>::error("Connection failed");
        }

        sendClientHelloIfNeeded();
        LOG_INFO(Network, "Connected to {}", url);
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    catch (const std::exception& e) {
        return Result<std::monostate, std::string>::error(
            std::string("Connection error: ") + e.what());
    }
}

void WebSocketService::sendClientHelloIfNeeded()
{
    if (protocol_ != Protocol::BINARY) {
        return;
    }
    if (helloSent_.exchange(true)) {
        return;
    }

    const auto payload = serialize_payload(clientHello_);
    MessageEnvelope hello{
        .id = 0,
        .message_type = kClientHelloMessageType,
        .payload = payload,
    };
    auto helloResult = sendBinary(serialize_envelope(hello));
    if (helloResult.isError()) {
        LOG_WARN(Network, "Failed to send binary hello message: {}", helloResult.errorValue());
        helloSent_ = false;
    }
}

void WebSocketService::disconnect()
{
    if (ws_) {
        ws_->onClosed([]() {});
        ws_->onError([](const std::string&) {});
        ws_->onMessage([](std::variant<rtc::binary, rtc::string>) {});
        if (ws_->isOpen()) {
            ws_->close();
        }
        ws_.reset();
    }
}

bool WebSocketService::isConnected() const
{
    return ws_ && ws_->isOpen();
}

Result<std::monostate, std::string> WebSocketService::sendText(const std::string& message)
{
    if (!isConnected()) {
        return Result<std::monostate, std::string>::error("Not connected");
    }

    try {
        ws_->send(message);
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    catch (const std::exception& e) {
        return Result<std::monostate, std::string>::error(std::string("Send failed: ") + e.what());
    }
}

Result<std::monostate, std::string> WebSocketService::sendBinary(const std::vector<std::byte>& data)
{
    if (!isConnected()) {
        return Result<std::monostate, std::string>::error("Not connected");
    }

    try {
        rtc::binary binaryMsg(data.begin(), data.end());
        ws_->send(binaryMsg);
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    catch (const std::exception& e) {
        return Result<std::monostate, std::string>::error(std::string("Send failed: ") + e.what());
    }
}

Result<std::monostate, std::string> WebSocketService::sendBinaryToDefaultPeer(
    const std::vector<std::byte>& data)
{
    std::shared_ptr<rtc::WebSocket> target;
    if (ws_ && ws_->isOpen()) {
        if (protocol_ == Protocol::JSON) {
            return Result<std::monostate, std::string>::error(
                "Binary send not supported while JSON protocol is active");
        }
        target = ws_;
    }
    else {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (const auto& ws : connectedClients_) {
            if (!ws || !ws->isOpen()) {
                continue;
            }
            auto protocolIt = clientProtocols_.find(ws);
            if (protocolIt == clientProtocols_.end() || protocolIt->second != Protocol::BINARY) {
                continue;
            }
            auto helloIt = clientHellos_.find(ws);
            if (helloIt == clientHellos_.end() || !isUiHello(helloIt->second)) {
                continue;
            }
            target = ws;
            break;
        }
    }

    if (!target) {
        return Result<std::monostate, std::string>::error("No UI peer available");
    }

    try {
        rtc::binary binaryMsg(data.begin(), data.end());
        target->send(binaryMsg);
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    catch (const std::exception& e) {
        return Result<std::monostate, std::string>::error(std::string("Send failed: ") + e.what());
    }
}

Result<MessageEnvelope, std::string> WebSocketService::sendBinaryAndReceive(
    const MessageEnvelope& envelope, int timeoutMs)
{
    uint64_t id = envelope.id;

    // Create pending request.
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingRequests_[id] = pending;
    }

    // Serialize and send.
    auto bytes = serialize_envelope(envelope);

    LOG_INFO(
        Network,
        "Sending binary (id={}, type={}, {} bytes)",
        id,
        envelope.message_type,
        bytes.size());

    auto sendResult = sendBinaryToDefaultPeer(bytes);
    if (sendResult.isError()) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingRequests_.erase(id);
        return Result<MessageEnvelope, std::string>::error(sendResult.errorValue());
    }

    // Wait for response.
    std::unique_lock<std::mutex> reqLock(pending->mutex);
    bool received = pending->cv.wait_for(
        reqLock, std::chrono::milliseconds(timeoutMs), [&pending]() { return pending->received; });

    // Clean up.
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingRequests_.erase(id);
    }

    if (!received) {
        return Result<MessageEnvelope, std::string>::error("Response timeout");
    }

    // Parse response.
    if (!pending->isBinary) {
        return Result<MessageEnvelope, std::string>::error(
            "Received text response when expecting binary");
    }

    try {
        auto& bytes = std::get<std::vector<std::byte>>(pending->response);
        MessageEnvelope responseEnvelope = deserialize_envelope(bytes);

        LOG_DEBUG(
            Network,
            "Received binary response (id={}, type={}, {} bytes)",
            responseEnvelope.id,
            responseEnvelope.message_type,
            bytes.size());

        return Result<MessageEnvelope, std::string>::okay(responseEnvelope);
    }
    catch (const std::exception& e) {
        return Result<MessageEnvelope, std::string>::error(
            std::string("Failed to deserialize response: ") + e.what());
    }
}

Result<std::string, ApiError> WebSocketService::sendJsonAndReceive(
    const std::string& message, int timeoutMs)
{
    if (!isConnected()) {
        return Result<std::string, ApiError>::error(ApiError{ "Not connected" });
    }

    // Generate correlation ID.
    uint64_t id = nextId_.fetch_add(1);

    // Inject ID into message.
    std::string messageWithId;
    try {
        nlohmann::json json = nlohmann::json::parse(message);
        json["id"] = id;
        messageWithId = json.dump();
    }
    catch (const std::exception& e) {
        return Result<std::string, ApiError>::error(
            ApiError{ std::string("Failed to inject correlation ID: ") + e.what() });
    }

    // Create pending request.
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingRequests_[id] = pending;
    }

    // Send.
    LOG_DEBUG(Network, "Sending JSON (id={}): {}", id, messageWithId);
    try {
        ws_->send(messageWithId);
    }
    catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingRequests_.erase(id);
        return Result<std::string, ApiError>::error(
            ApiError{ std::string("Send failed: ") + e.what() });
    }

    // Wait for response.
    std::unique_lock<std::mutex> reqLock(pending->mutex);
    bool received = pending->cv.wait_for(
        reqLock, std::chrono::milliseconds(timeoutMs), [&pending]() { return pending->received; });

    // Clean up.
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingRequests_.erase(id);
    }

    if (!received) {
        return Result<std::string, ApiError>::error(ApiError{ "Response timeout" });
    }

    if (pending->isBinary) {
        return Result<std::string, ApiError>::error(
            ApiError{ "Received binary response when expecting text" });
    }

    LOG_DEBUG(
        Network,
        "Received JSON response (id={}, {} bytes)",
        id,
        std::get<std::string>(pending->response).size());

    return Result<std::string, ApiError>::okay(std::get<std::string>(pending->response));
}

Result<std::monostate, std::string> WebSocketService::listen(
    uint16_t port, const std::string& bindAddress)
{
    try {
        LOG_INFO(Network, "Starting server on port {}", port);

        // Create WebSocket server configuration.
        // Default maxMessageSize is 256 KB which is too small for RenderMessage.
        // 200x200 world with DebugCell (~23 bytes/cell) = ~920 KB.
        rtc::WebSocketServerConfiguration config;
        config.port = port;
        config.bindAddress = bindAddress;
        config.enableTls = false;                 // No TLS for now.
        config.maxMessageSize = 16 * 1024 * 1024; // 16 MB.

        // Create server.
        server_ = std::make_unique<rtc::WebSocketServer>(config);

        // Set up client connection handler.
        server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws) { onClientConnected(ws); });

        LOG_INFO(Network, "Server started on port {}", port);
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    catch (const std::exception& e) {
        return Result<std::monostate, std::string>::error(
            std::string("Failed to start server: ") + e.what());
    }
}

void WebSocketService::stopListening()
{
    stopListening(true);
}

void WebSocketService::stopListening(bool disconnectClients)
{
    if (!server_) {
        return;
    }

    if (disconnectClients) {
        std::vector<std::shared_ptr<rtc::WebSocket>> clients;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clients = connectedClients_;
            connectedClients_.clear();
            clientProtocols_.clear();
            clientRenderFormats_.clear();
            clientHellos_.clear();
            connectionRegistry_.clear();
            connectionIds_.clear();
        }

        for (auto& ws : clients) {
            if (!ws) {
                continue;
            }
            ws->onClosed([]() {});
            ws->onError([](const std::string&) {});
            ws->onMessage([](std::variant<rtc::binary, rtc::string>) {});
            if (ws->isOpen()) {
                ws->close();
            }
        }
    }

    server_->stop();
    server_.reset();
    LOG_INFO(Network, "Server stopped");
}

bool WebSocketService::isListening() const
{
    return server_ != nullptr;
}

void WebSocketService::closeNonLocalClients()
{
    std::vector<std::shared_ptr<rtc::WebSocket>> toClose;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (const auto& ws : connectedClients_) {
            if (!ws || !ws->isOpen()) {
                continue;
            }
            const auto remoteAddress = ws->remoteAddress();
            const bool isLocal = remoteAddress.has_value()
                && isLoopbackHost(extractHostFromRemoteAddress(remoteAddress.value()));
            if (!isLocal) {
                toClose.push_back(ws);
            }
        }
    }

    for (const auto& ws : toClose) {
        ws->close();
    }
}

void WebSocketService::broadcastBinary(const std::vector<std::byte>& data)
{
    std::vector<std::shared_ptr<rtc::WebSocket>> clients;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (const auto& ws : connectedClients_) {
            if (!ws || !ws->isOpen()) {
                continue;
            }
            auto helloIt = clientHellos_.find(ws);
            if (helloIt != clientHellos_.end() && helloIt->second.wantsEvents) {
                clients.push_back(ws);
            }
        }
    }

    if (clients.empty()) {
        return;
    }

    // Convert to rtc::binary.
    rtc::binary binaryMsg(data.begin(), data.end());

    LOG_INFO(Network, "Broadcasting binary ({} bytes) to {} clients", data.size(), clients.size());

    // Send to all connected clients.
    for (auto& ws : clients) {
        if (ws && ws->isOpen()) {
            try {
                ws->send(binaryMsg);
            }
            catch (const std::exception& e) {
                LOG_ERROR(Network, "Broadcast failed for client: {}", e.what());
            }
        }
    }
}

void WebSocketService::broadcastRenderMessage(
    const WorldData& data, const std::vector<OrganismId>& organism_grid)
{
    std::vector<std::pair<std::shared_ptr<rtc::WebSocket>, RenderFormat::EnumType>> renderFormats;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        if (clientRenderFormats_.empty()) {
            LOG_INFO(Network, "broadcastRenderMessage called but clientRenderFormats_ is EMPTY");
            return;
        }
        renderFormats.assign(clientRenderFormats_.begin(), clientRenderFormats_.end());
    }

    LOG_INFO(
        Network,
        "Broadcasting RenderMessage to {} subscribed clients (step {})",
        renderFormats.size(),
        data.timestep);

    for (const auto& [ws, format] : renderFormats) {
        if (ws && ws->isOpen()) {
            try {
                RenderMessage msg =
                    RenderMessageUtils::packRenderMessage(data, format, organism_grid);

                std::vector<std::byte> msgData;
                zpp::bits::out out(msgData);
                out(msg).or_throw();

                rtc::binary binaryMsg(msgData.begin(), msgData.end());
                ws->send(binaryMsg);

                LOG_INFO(
                    Network,
                    "Sent RenderMessage ({} bytes, format={}) to client",
                    msgData.size(),
                    static_cast<int>(format));
            }
            catch (const std::exception& e) {
                LOG_ERROR(Network, "RenderMessage broadcast failed: {}", e.what());
            }
        }
    }
}

void WebSocketService::setClientRenderFormat(
    std::shared_ptr<rtc::WebSocket> ws, RenderFormat::EnumType format)
{
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clientRenderFormats_[ws] = format;
    }
    LOG_INFO(
        Network,
        "Client render format set to {}",
        format == RenderFormat::EnumType::Basic ? "Basic" : "Debug");
}

RenderFormat::EnumType WebSocketService::getClientRenderFormat(
    std::shared_ptr<rtc::WebSocket> ws) const
{
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clientRenderFormats_.find(ws);
        if (it != clientRenderFormats_.end()) {
            return it->second;
        }
    }
    return RenderFormat::EnumType::Basic;
}

std::shared_ptr<rtc::WebSocket> WebSocketService::getClientByConnectionId(
    const std::string& connectionId)
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = connectionRegistry_.find(connectionId);
    if (it != connectionRegistry_.end()) {
        return it->second.lock();
    }
    return nullptr;
}

std::string WebSocketService::getConnectionId(std::shared_ptr<rtc::WebSocket> ws)
{
    // Check if we already have an ID for this connection.
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = connectionIds_.find(ws);
        if (it != connectionIds_.end()) {
            return it->second;
        }
    }

    // Generate new ID.
    uint64_t id = nextConnectionId_.fetch_add(1);
    std::string connectionId = "conn_" + std::to_string(id);

    // Register in both maps.
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        connectionIds_[ws] = connectionId;
        connectionRegistry_[connectionId] = ws;
    }

    LOG_DEBUG(Network, "Assigned connection ID '{}' to client", connectionId);
    return connectionId;
}

bool WebSocketService::clientWantsEvents(const std::string& connectionId) const
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = connectionRegistry_.find(connectionId);
    if (it == connectionRegistry_.end()) {
        return false;
    }

    auto ws = it->second.lock();
    if (!ws) {
        return false;
    }

    auto helloIt = clientHellos_.find(ws);
    if (helloIt == clientHellos_.end()) {
        return false;
    }

    return helloIt->second.wantsEvents;
}

bool WebSocketService::clientWantsRender(const std::string& connectionId) const
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = connectionRegistry_.find(connectionId);
    if (it == connectionRegistry_.end()) {
        return false;
    }

    auto ws = it->second.lock();
    if (!ws) {
        return false;
    }

    auto helloIt = clientHellos_.find(ws);
    if (helloIt == clientHellos_.end()) {
        return false;
    }

    return helloIt->second.wantsRender;
}

Result<std::monostate, std::string> WebSocketService::sendToClient(
    const std::string& connectionId, const std::string& message)
{
    // Look up the connection.
    std::shared_ptr<rtc::WebSocket> ws;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = connectionRegistry_.find(connectionId);
        if (it == connectionRegistry_.end()) {
            return Result<std::monostate, std::string>::error(
                "Unknown connection ID: " + connectionId);
        }
        ws = it->second.lock();
    }

    // Check if connection is still alive.
    if (!ws || !ws->isOpen()) {
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            connectionRegistry_.erase(connectionId);
        }
        return Result<std::monostate, std::string>::error("Connection closed: " + connectionId);
    }

    // Send the message.
    try {
        ws->send(message);
        LOG_DEBUG(Network, "Sent message to {} ({} bytes)", connectionId, message.size());
        return Result<std::monostate, std::string>::okay({});
    }
    catch (const std::exception& e) {
        return Result<std::monostate, std::string>::error(std::string("Send failed: ") + e.what());
    }
}

Result<std::monostate, std::string> WebSocketService::sendToClient(
    const std::string& connectionId, const std::vector<std::byte>& data)
{
    // Look up the connection.
    std::shared_ptr<rtc::WebSocket> ws;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = connectionRegistry_.find(connectionId);
        if (it == connectionRegistry_.end()) {
            return Result<std::monostate, std::string>::error(
                "Unknown connection ID: " + connectionId);
        }
        ws = it->second.lock();
    }

    // Check if connection is still alive.
    if (!ws || !ws->isOpen()) {
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            connectionRegistry_.erase(connectionId);
        }
        return Result<std::monostate, std::string>::error("Connection closed: " + connectionId);
    }

    // Send binary data.
    try {
        rtc::binary binaryMsg(data.begin(), data.end());
        ws->send(binaryMsg);
        LOG_DEBUG(Network, "Sent binary to {} ({} bytes)", connectionId, data.size());
        return Result<std::monostate, std::string>::okay({});
    }
    catch (const std::exception& e) {
        return Result<std::monostate, std::string>::error(std::string("Send failed: ") + e.what());
    }
}

void WebSocketService::onClientConnected(std::shared_ptr<rtc::WebSocket> ws)
{
    ws->onOpen([this, ws]() {
        const auto remoteAddress = ws->remoteAddress();
        const bool isLocal = remoteAddress.has_value()
            && isLoopbackHost(extractHostFromRemoteAddress(remoteAddress.value()));
        if (!isLocal) {
            const std::string token = extractTokenFromPath(ws->path());
            std::string accessToken;
            {
                std::lock_guard<std::mutex> lock(accessTokenMutex_);
                accessToken = accessToken_;
            }
            const std::string remoteLabel = remoteAddress.value_or("unknown");
            if (accessToken.empty()) {
                LOG_WARN(
                    Network,
                    "Rejecting non-local client connection from {} (token not configured)",
                    remoteLabel);
                std::this_thread::sleep_for(kAuthRejectDelay);
                ws->close();
                return;
            }
            if (token.empty()) {
                LOG_WARN(
                    Network,
                    "Rejecting non-local client connection from {} (token missing)",
                    remoteLabel);
                std::this_thread::sleep_for(kAuthRejectDelay);
                ws->close();
                return;
            }
            if (token != accessToken) {
                LOG_WARN(
                    Network,
                    "Rejecting non-local client connection from {} (token mismatch)",
                    remoteLabel);
                std::this_thread::sleep_for(kAuthRejectDelay);
                ws->close();
                return;
            }
            std::this_thread::sleep_for(kAuthAcceptDelay);
        }

        LOG_INFO(Network, "Client connected");
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            connectedClients_.push_back(ws);
        }

        // Set up message handler for this client.
        ws->onMessage([this, ws](std::variant<rtc::binary, rtc::string> data) {
            if (std::holds_alternative<rtc::binary>(data)) {
                // Binary message - route to command handlers.
                const rtc::binary& binaryData = std::get<rtc::binary>(data);
                onClientMessage(ws, binaryData);
            }
            else {
                // Text/JSON message - route to JSON command handlers.
                const rtc::string& textData = std::get<rtc::string>(data);
                onClientMessageJson(ws, textData);
            }
        });

        // Set up close handler.
        ws->onClosed([this, ws]() {
            LOG_INFO(Network, "Client disconnected");

            // Get connection ID before cleanup (needed for callback).
            std::string connectionId;
            {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                auto idIt = connectionIds_.find(ws);
                if (idIt != connectionIds_.end()) {
                    connectionId = idIt->second;
                    connectionRegistry_.erase(connectionId);
                    connectionIds_.erase(idIt);
                }

                // Clean up internal state.
                connectedClients_.erase(
                    std::remove(connectedClients_.begin(), connectedClients_.end(), ws),
                    connectedClients_.end());
                clientProtocols_.erase(ws);
                clientRenderFormats_.erase(ws);
                clientHellos_.erase(ws);
            }

            if (!connectionId.empty() && clientDisconnectCallback_) {
                clientDisconnectCallback_(connectionId);
            }
        });

        // Set up error handler.
        ws->onError([](std::string error) { LOG_ERROR(Network, "Client error: {}", error); });
    });
}

void WebSocketService::onClientMessage(std::shared_ptr<rtc::WebSocket> ws, const rtc::binary& data)
{
    LOG_DEBUG(Network, "Received binary message ({} bytes)", data.size());

    // Track that this client uses binary protocol.
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clientProtocols_[ws] = Protocol::BINARY;
    }

    // Convert to std::vector<std::byte>.
    std::vector<std::byte> bytes(data.size());
    std::memcpy(bytes.data(), data.data(), data.size());

    // Deserialize envelope.
    MessageEnvelope envelope;
    try {
        envelope = deserialize_envelope(bytes);
    }
    catch (const std::exception& e) {
        LOG_ERROR(Network, "Failed to deserialize envelope: {}", e.what());
        return;
    }

    LOG_DEBUG(
        Network,
        "Command '{}', id={}, payload={} bytes",
        envelope.message_type,
        envelope.id,
        envelope.payload.size());

    if (envelope.id > 0 && isResponseMessageType(envelope.message_type)) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        auto it = pendingRequests_.find(envelope.id);
        if (it != pendingRequests_.end()) {
            auto& pending = it->second;
            std::lock_guard<std::mutex> reqLock(pending->mutex);
            pending->response = bytes;
            pending->isBinary = true;
            pending->received = true;
            pending->cv.notify_one();
            return;
        }

        LOG_DEBUG(
            Network,
            "WebSocketService SERVER: Response {} not found (already processed)",
            envelope.id);
        return;
    }

    if (envelope.id == 0) {
        if (envelope.message_type == kClientHelloMessageType) {
            ClientHello hello{};
            if (!envelope.payload.empty()) {
                try {
                    hello = deserialize_payload<ClientHello>(envelope.payload);
                }
                catch (const std::exception& e) {
                    LOG_WARN(Network, "Failed to parse ClientHello: {}", e.what());
                    return;
                }
            }
            else {
                hello.protocolVersion = 0;
            }

            if (hello.protocolVersion != kClientHelloProtocolVersion) {
                LOG_WARN(
                    Network,
                    "ClientHello protocol mismatch (client={}, server={})",
                    hello.protocolVersion,
                    kClientHelloProtocolVersion);
                ws->close();
                return;
            }

            const bool isUiClient = isUiHello(hello);
            bool reject = false;
            {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                if (isUiClient) {
                    for (const auto& [client, existing] : clientHellos_) {
                        if (client != ws && isUiHello(existing)) {
                            reject = true;
                            break;
                        }
                    }
                }

                if (!reject) {
                    clientHellos_[ws] = hello;
                }
            }

            if (reject) {
                LOG_WARN(Network, "Rejecting second UI client connection");
                ws->close();
            }
            else {
                LOG_INFO(
                    Network,
                    "ClientHello accepted (mode={}, protocol_version={}, wants_render={}, "
                    "wants_events={})",
                    isUiClient ? "ui" : "control-only",
                    hello.protocolVersion,
                    hello.wantsRender,
                    hello.wantsEvents);
            }

            return;
        }
        LOG_WARN(Network, "Ignoring client push '{}'", envelope.message_type);
        return;
    }

    // Look up handler.
    auto it = commandHandlers_.find(envelope.message_type);
    if (it == commandHandlers_.end()) {
        LOG_WARN(Network, "No handler for command '{}'", envelope.message_type);
        // TODO: Send error response.
        return;
    }

    // Call handler.
    it->second(envelope.payload, ws, envelope.id);
}

void WebSocketService::onClientMessageJson(
    std::shared_ptr<rtc::WebSocket> ws, const std::string& jsonText)
{
    LOG_DEBUG(Network, "Received JSON message ({} bytes)", jsonText.size());

    // Track that this client uses JSON protocol.
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clientProtocols_[ws] = Protocol::JSON;
    }

    // Parse JSON to extract command name and correlation ID.
    nlohmann::json jsonMsg;
    try {
        jsonMsg = nlohmann::json::parse(jsonText);
    }
    catch (const nlohmann::json::exception& e) {
        LOG_ERROR(Network, "Failed to parse JSON: {}", e.what());
        return;
    }

    // Extract command name and correlation ID.
    std::string commandName;
    uint64_t correlationId = 0;

    if (jsonMsg.contains("command")) {
        commandName = jsonMsg["command"].get<std::string>();
    }
    else {
        LOG_ERROR(Network, "JSON message missing 'command' field");
        return;
    }

    if (jsonMsg.contains("id")) {
        correlationId = jsonMsg["id"].get<uint64_t>();
    }

    LOG_DEBUG(Network, "JSON command '{}', id={}", commandName, correlationId);

    // Check if JSON deserializer is configured.
    if (!jsonDeserializer_) {
        LOG_ERROR(Network, "No JSON deserializer configured - ignoring JSON message");
        ws->send(
            makeJsonErrorResponse(correlationId, "JSON protocol not configured on this service")
                .dump());
        return;
    }

    // Deserialize JSON command using injected deserializer.
    std::any cmdAny;
    try {
        cmdAny = jsonDeserializer_(jsonText);
    }
    catch (const std::exception& e) {
        LOG_ERROR(Network, "JSON deserialization failed: {}", e.what());
        ws->send(makeJsonErrorResponse(correlationId, e.what()).dump());
        return;
    }

    // Check if JSON dispatcher is configured.
    if (!jsonDispatcher_) {
        LOG_ERROR(Network, "No JSON dispatcher configured - ignoring JSON command");
        ws->send(
            makeJsonErrorResponse(correlationId, "JSON dispatcher not configured on this service")
                .dump());
        return;
    }

    // Create handler invoker that dispatcher can use to call registered handlers.
    auto invokeHandler =
        [this,
         ws](std::string commandName, std::vector<std::byte> payload, uint64_t correlationId) {
            auto it = commandHandlers_.find(commandName);
            if (it != commandHandlers_.end()) {
                it->second(payload, ws, correlationId);
            }
            else {
                LOG_WARN(Network, "No handler registered for '{}'", commandName);
            }
        };

    // Dispatch to injected handler (server/UI provides the implementation).
    jsonDispatcher_(cmdAny, ws, correlationId, invokeHandler);
}

} // namespace Network
} // namespace DirtSim
