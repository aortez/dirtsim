#include "WebSocketService.h"
#include "server/api/ApiCommand.h"
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>
#include <thread>

namespace DirtSim {
namespace Network {

WebSocketService::WebSocketService()
{
    spdlog::debug("WebSocketService created");
}

WebSocketService::~WebSocketService()
{
    disconnect();
    stopListening();
}

Result<std::monostate, std::string> WebSocketService::connect(const std::string& url, int timeoutMs)
{
    try {
        spdlog::debug("WebSocketService: Connecting to {}", url);

        // Reset connection state.
        connectionFailed_ = false;
        url_ = url;

        // Create WebSocket with large message size.
        rtc::WebSocketConfiguration config;
        config.maxMessageSize = 10 * 1024 * 1024; // 10MB limit.

        ws_ = std::make_shared<rtc::WebSocket>(config);

        // Set up message handler.
        ws_->onMessage([this](std::variant<rtc::binary, rtc::string> data) {
            if (std::holds_alternative<rtc::string>(data)) {
                // JSON text message.
                std::string message = std::get<rtc::string>(data);
                spdlog::debug("WebSocketService: Received text ({} bytes)", message.size());

                // Extract correlation ID.
                std::optional<uint64_t> correlationId;
                try {
                    nlohmann::json json = nlohmann::json::parse(message);
                    if (json.contains("id") && json["id"].is_number()) {
                        correlationId = json["id"].get<uint64_t>();
                    }
                }
                catch (...) {
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
                spdlog::debug("WebSocketService: Received binary ({} bytes)", binaryData.size());

                // Convert to std::vector<std::byte>.
                std::vector<std::byte> bytes(binaryData.size());
                std::memcpy(bytes.data(), binaryData.data(), binaryData.size());

                // Try to extract correlation ID from envelope.
                std::optional<uint64_t> correlationId;
                try {
                    MessageEnvelope envelope = deserialize_envelope(bytes);
                    correlationId = envelope.id;
                }
                catch (...) {
                    // Not an envelope or corrupted - pass to callback.
                }

                if (correlationId.has_value()) {
                    // Route to pending request.
                    std::lock_guard<std::mutex> lock(pendingMutex_);
                    auto it = pendingRequests_.find(*correlationId);
                    if (it != pendingRequests_.end()) {
                        auto& pending = it->second;
                        std::lock_guard<std::mutex> reqLock(pending->mutex);
                        pending->response = bytes;
                        pending->isBinary = true;
                        pending->received = true;
                        pending->cv.notify_one();
                    }
                }
                else if (binaryCallback_) {
                    binaryCallback_(bytes);
                }
            }
        });

        // Set up open handler.
        ws_->onOpen([this]() {
            spdlog::debug("WebSocketService: Connection opened");
            if (connectedCallback_) {
                connectedCallback_();
            }
        });

        // Set up close handler.
        ws_->onClosed([this]() {
            spdlog::debug("WebSocketService: Connection closed");
            connectionFailed_ = true;
            if (disconnectedCallback_) {
                disconnectedCallback_();
            }
        });

        // Set up error handler.
        ws_->onError([this](std::string error) {
            spdlog::error("WebSocketService error: {}", error);
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

        spdlog::info("WebSocketService: Connected to {}", url);
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

        spdlog::debug(
            "WebSocketService: Sending binary (id={}, type={}, {} bytes)",
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

        spdlog::debug(
            "WebSocketService: Received binary response (id={}, type={}, {} bytes)",
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
    spdlog::debug("WebSocketService: Sending JSON (id={}): {}", id, messageWithId);
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

    spdlog::debug(
        "WebSocketService: Received JSON response (id={}, {} bytes)",
        id,
        std::get<std::string>(pending->response).size());

    return Result<std::string, ApiError>::okay(std::get<std::string>(pending->response));
}

// =============================================================================
// Server-side methods (listening for connections).
// =============================================================================

Result<std::monostate, std::string> WebSocketService::listen(uint16_t port)
{
    try {
        spdlog::info("WebSocketService: Starting server on port {}", port);

        // Create WebSocket server configuration.
        rtc::WebSocketServerConfiguration config;
        config.port = port;
        config.bindAddress = "0.0.0.0";           // Listen on all interfaces.
        config.enableTls = false;                 // No TLS for now.
        config.maxMessageSize = 10 * 1024 * 1024; // 10MB limit.

        // Create server.
        server_ = std::make_unique<rtc::WebSocketServer>(config);

        // Set up client connection handler.
        server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws) { onClientConnected(ws); });

        spdlog::info("WebSocketService: Server started on port {}", port);
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
        spdlog::info("WebSocketService: Server stopped");
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

    spdlog::trace(
        "WebSocketService: Broadcasting binary ({} bytes) to {} clients",
        data.size(),
        connectedClients_.size());

    // Send to all connected clients.
    for (auto& ws : connectedClients_) {
        if (ws && ws->isOpen()) {
            try {
                ws->send(binaryMsg);
            }
            catch (const std::exception& e) {
                spdlog::error("WebSocketService: Broadcast failed for client: {}", e.what());
            }
        }
    }
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

    spdlog::debug("WebSocketService: Assigned connection ID '{}' to client", connectionId);
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
        spdlog::debug(
            "WebSocketService: Sent message to {} ({} bytes)", connectionId, message.size());
        return Result<std::monostate, std::string>::okay({});
    }
    catch (const std::exception& e) {
        return Result<std::monostate, std::string>::error(std::string("Send failed: ") + e.what());
    }
}

void WebSocketService::onClientConnected(std::shared_ptr<rtc::WebSocket> ws)
{
    spdlog::info("WebSocketService: Client connected");
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
        spdlog::info("WebSocketService: Client disconnected");
        connectedClients_.erase(
            std::remove(connectedClients_.begin(), connectedClients_.end(), ws),
            connectedClients_.end());
        clientProtocols_.erase(ws);

        // Clean up connection registry.
        auto idIt = connectionIds_.find(ws);
        if (idIt != connectionIds_.end()) {
            connectionRegistry_.erase(idIt->second);
            connectionIds_.erase(idIt);
        }
    });

    // Set up error handler.
    ws->onError(
        [](std::string error) { spdlog::error("WebSocketService: Client error: {}", error); });
}

void WebSocketService::onClientMessage(std::shared_ptr<rtc::WebSocket> ws, const rtc::binary& data)
{
    spdlog::debug("WebSocketService: Received binary message ({} bytes)", data.size());

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
        spdlog::error("WebSocketService: Failed to deserialize envelope: {}", e.what());
        return;
    }

    spdlog::debug(
        "WebSocketService: Command '{}', id={}, payload={} bytes",
        envelope.message_type,
        envelope.id,
        envelope.payload.size());

    // Look up handler.
    auto it = commandHandlers_.find(envelope.message_type);
    if (it == commandHandlers_.end()) {
        spdlog::warn("WebSocketService: No handler for command '{}'", envelope.message_type);
        // TODO: Send error response.
        return;
    }

    // Call handler.
    it->second(envelope.payload, ws, envelope.id);
}

void WebSocketService::onClientMessageJson(
    std::shared_ptr<rtc::WebSocket> ws, const std::string& jsonText)
{
    spdlog::debug("WebSocketService: Received JSON message ({} bytes)", jsonText.size());

    // Track that this client uses JSON protocol.
    clientProtocols_[ws] = Protocol::JSON;

    // Parse JSON to extract command name and correlation ID.
    nlohmann::json jsonMsg;
    try {
        jsonMsg = nlohmann::json::parse(jsonText);
    }
    catch (const nlohmann::json::exception& e) {
        spdlog::error("WebSocketService: Failed to parse JSON: {}", e.what());
        return;
    }

    // Extract command name and correlation ID.
    std::string commandName;
    uint64_t correlationId = 0;

    if (jsonMsg.contains("command")) {
        commandName = jsonMsg["command"].get<std::string>();
    }
    else {
        spdlog::error("WebSocketService: JSON message missing 'command' field");
        return;
    }

    if (jsonMsg.contains("id")) {
        correlationId = jsonMsg["id"].get<uint64_t>();
    }

    spdlog::debug("WebSocketService: JSON command '{}', id={}", commandName, correlationId);

    // Check if JSON deserializer is configured.
    if (!jsonDeserializer_) {
        spdlog::error("WebSocketService: No JSON deserializer configured - ignoring JSON message");
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
        spdlog::error("WebSocketService: JSON deserialization failed: {}", e.what());
        nlohmann::json errorResponse = { { "id", correlationId }, { "error", e.what() } };
        ws->send(errorResponse.dump());
        return;
    }

    // Check if JSON dispatcher is configured.
    if (!jsonDispatcher_) {
        spdlog::error("WebSocketService: No JSON dispatcher configured - ignoring JSON command");
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
                spdlog::warn("WebSocketService: No handler registered for '{}'", commandName);
            }
        };

    // Dispatch to injected handler (server/UI provides the implementation).
    jsonDispatcher_(cmdAny, ws, correlationId, invokeHandler);
}

} // namespace Network
} // namespace DirtSim
