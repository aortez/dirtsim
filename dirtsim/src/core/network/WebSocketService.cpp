#include "WebSocketService.h"
#include "core/LoggingChannels.h"
#include "core/RenderMessageUtils.h"
#include "core/WorldData.h"
#include "server/api/ApiCommand.h"
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>
#include <thread>
#include <zpp_bits.h>

namespace DirtSim {
namespace Network {

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
        url_ = url;

        // Create WebSocket with large message size.
        rtc::WebSocketConfiguration config;

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
                    LOG_INFO(Network, "I'm lost  !!!!!!!!!!!!!");
                    // Not JSON or no ID - that's fine.
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
                    else if (envelope.id > 0) {
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

        LOG_INFO(Network, "Connected to {}", url);
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    catch (const std::exception& e) {
        return Result<std::monostate, std::string>::error(
            std::string("Connection error: ") + e.what());
    }
}

void WebSocketService::disconnect()
{
    if (ws_) {
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

Result<MessageEnvelope, std::string> WebSocketService::sendBinaryAndReceive(
    const MessageEnvelope& envelope, int timeoutMs)
{
    if (!isConnected()) {
        return Result<MessageEnvelope, std::string>::error("Not connected");
    }

    uint64_t id = envelope.id;

    // Create pending request.
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingRequests_[id] = pending;
    }

    // Serialize and send.
    try {
        auto bytes = serialize_envelope(envelope);
        rtc::binary binaryMsg(bytes.begin(), bytes.end());

        LOG_INFO(
            Network,
            "Sending binary (id={}, type={}, {} bytes)",
            id,
            envelope.message_type,
            bytes.size());

        ws_->send(binaryMsg);
    }
    catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingRequests_.erase(id);
        return Result<MessageEnvelope, std::string>::error(std::string("Send failed: ") + e.what());
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

Result<std::monostate, std::string> WebSocketService::listen(uint16_t port)
{
    try {
        LOG_INFO(Network, "Starting server on port {}", port);

        // Create WebSocket server configuration.
        rtc::WebSocketServerConfiguration config;
        config.port = port;
        config.bindAddress = "0.0.0.0"; // Listen on all interfaces.
        config.enableTls = false;       // No TLS for now.

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
    if (server_) {
        server_->stop();
        server_.reset();
        LOG_INFO(Network, "Server stopped");
    }
}

bool WebSocketService::isListening() const
{
    return server_ != nullptr;
}

void WebSocketService::broadcastBinary(const std::vector<std::byte>& data)
{
    if (connectedClients_.empty()) {
        return;
    }

    // Convert to rtc::binary.
    rtc::binary binaryMsg(data.begin(), data.end());

    LOG_INFO(
        Network,
        "Broadcasting binary ({} bytes) to {} clients",
        data.size(),
        connectedClients_.size());

    // Send to all connected clients.
    for (auto& ws : connectedClients_) {
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

void WebSocketService::broadcastRenderMessage(const WorldData& data)
{
    if (clientRenderFormats_.empty()) {
        LOG_INFO(Network, "broadcastRenderMessage called but clientRenderFormats_ is EMPTY");
        return;
    }

    LOG_INFO(
        Network,
        "Broadcasting RenderMessage to {} subscribed clients (step {})",
        clientRenderFormats_.size(),
        data.timestep);

    for (const auto& [ws, format] : clientRenderFormats_) {
        if (ws && ws->isOpen()) {
            try {
                RenderMessage msg = RenderMessageUtils::packRenderMessage(data, format);

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
    std::shared_ptr<rtc::WebSocket> ws, RenderFormat format)
{
    clientRenderFormats_[ws] = format;
    LOG_INFO(
        Network,
        "Client render format set to {}",
        format == RenderFormat::BASIC ? "BASIC" : "DEBUG");
}

RenderFormat WebSocketService::getClientRenderFormat(std::shared_ptr<rtc::WebSocket> ws) const
{
    auto it = clientRenderFormats_.find(ws);
    if (it != clientRenderFormats_.end()) {
        return it->second;
    }
    return RenderFormat::BASIC;
}

std::shared_ptr<rtc::WebSocket> WebSocketService::getClientByConnectionId(
    const std::string& connectionId)
{
    auto it = connectionRegistry_.find(connectionId);
    if (it != connectionRegistry_.end()) {
        return it->second.lock();
    }
    return nullptr;
}

std::string WebSocketService::getConnectionId(std::shared_ptr<rtc::WebSocket> ws)
{
    // Check if we already have an ID for this connection.
    auto it = connectionIds_.find(ws);
    if (it != connectionIds_.end()) {
        return it->second;
    }

    // Generate new ID.
    uint64_t id = nextConnectionId_.fetch_add(1);
    std::string connectionId = "conn_" + std::to_string(id);

    // Register in both maps.
    connectionIds_[ws] = connectionId;
    connectionRegistry_[connectionId] = ws;

    LOG_DEBUG(Network, "Assigned connection ID '{}' to client", connectionId);
    return connectionId;
}

Result<std::monostate, std::string> WebSocketService::sendToClient(
    const std::string& connectionId, const std::string& message)
{
    // Look up the connection.
    auto it = connectionRegistry_.find(connectionId);
    if (it == connectionRegistry_.end()) {
        return Result<std::monostate, std::string>::error("Unknown connection ID: " + connectionId);
    }

    // Check if connection is still alive.
    auto ws = it->second.lock();
    if (!ws || !ws->isOpen()) {
        // Clean up stale entry.
        connectionRegistry_.erase(it);
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
    auto it = connectionRegistry_.find(connectionId);
    if (it == connectionRegistry_.end()) {
        return Result<std::monostate, std::string>::error("Unknown connection ID: " + connectionId);
    }

    // Check if connection is still alive.
    auto ws = it->second.lock();
    if (!ws || !ws->isOpen()) {
        connectionRegistry_.erase(it);
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
    LOG_INFO(Network, "Client connected");
    connectedClients_.push_back(ws);

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
        connectedClients_.erase(
            std::remove(connectedClients_.begin(), connectedClients_.end(), ws),
            connectedClients_.end());
        clientProtocols_.erase(ws);
        clientRenderFormats_.erase(ws);

        // Clean up connection registry.
        auto idIt = connectionIds_.find(ws);
        if (idIt != connectionIds_.end()) {
            connectionRegistry_.erase(idIt->second);
            connectionIds_.erase(idIt);
        }
    });

    // Set up error handler.
    ws->onError([](std::string error) { LOG_ERROR(Network, "Client error: {}", error); });
}

void WebSocketService::onClientMessage(std::shared_ptr<rtc::WebSocket> ws, const rtc::binary& data)
{
    LOG_DEBUG(Network, "Received binary message ({} bytes)", data.size());

    // Track that this client uses binary protocol.
    clientProtocols_[ws] = Protocol::BINARY;

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
    clientProtocols_[ws] = Protocol::JSON;

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
        nlohmann::json errorResponse = {
            { "id", correlationId }, { "error", "JSON protocol not configured on this service" }
        };
        ws->send(errorResponse.dump());
        return;
    }

    // Deserialize JSON command using injected deserializer.
    std::any cmdAny;
    try {
        cmdAny = jsonDeserializer_(jsonText);
    }
    catch (const std::exception& e) {
        LOG_ERROR(Network, "JSON deserialization failed: {}", e.what());
        nlohmann::json errorResponse = { { "id", correlationId }, { "error", e.what() } };
        ws->send(errorResponse.dump());
        return;
    }

    // Check if JSON dispatcher is configured.
    if (!jsonDispatcher_) {
        LOG_ERROR(Network, "No JSON dispatcher configured - ignoring JSON command");
        nlohmann::json errorResponse = {
            { "id", correlationId }, { "error", "JSON dispatcher not configured on this service" }
        };
        ws->send(errorResponse.dump());
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
