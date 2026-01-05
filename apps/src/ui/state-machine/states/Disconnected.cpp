#include "State.h"
#include "core/LoggingChannels.h"
#include "core/RenderMessage.h"
#include "core/RenderMessageFull.h"
#include "core/RenderMessageUtils.h"
#include "core/ScenarioConfig.h"
#include "core/WorldData.h"
#include "core/api/UiUpdateEvent.h"
#include "core/network/WebSocketService.h"
#include "ui/state-machine/api/DrawDebugToggle.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/state-machine/network/MessageParser.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Ui {
namespace State {

void Disconnected::onEnter(StateMachine& sm)
{
    sm_ = &sm;
    LOG_INFO(State, "Entered Disconnected state (retry_count={}, retry_pending={})",
        retry_count_, retry_pending_);
}

void Disconnected::updateAnimations()
{
    if (!retry_pending_ || !sm_) {
        return;
    }

    // Check if enough time has passed since last attempt.
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_attempt_time_).count();

    if (elapsed >= RETRY_INTERVAL_SECONDS) {
        if (retry_count_ >= MAX_RETRY_ATTEMPTS) {
            LOG_ERROR(State, "Connection failed after {} retry attempts, giving up",
                MAX_RETRY_ATTEMPTS);
            retry_pending_ = false;
            return;
        }

        LOG_INFO(State, "Retrying connection to {}:{} (attempt {}/{})",
            pending_host_, pending_port_, retry_count_ + 1, MAX_RETRY_ATTEMPTS);

        // Queue a new connection attempt.
        sm_->queueEvent(ConnectToServerCommand{ pending_host_, pending_port_ });
    }
}

State::Any Disconnected::onEvent(const ConnectToServerCommand& cmd, StateMachine& sm)
{
    LOG_INFO(State, "Connect command received (host={}, port={})", cmd.host, cmd.port);

    auto& wsService = sm.getWebSocketService();

    // Setup callbacks before connecting.
    wsService.onConnected([&sm]() {
        LOG_INFO(Network, "Connected to server");
        sm.queueEvent(ServerConnectedEvent{});
    });

    wsService.onDisconnected([&sm]() {
        LOG_WARN(Network, "Disconnected from server");
        sm.queueEvent(ServerDisconnectedEvent{ "Connection closed" });
    });

    wsService.onError([&sm](const std::string& error) {
        LOG_ERROR(Network, "Connection error: {}", error);
        sm.queueEvent(ServerDisconnectedEvent{ error });
    });

    // Setup callback for server-pushed commands (e.g., DrawDebugToggle from gamepad).
    wsService.onServerCommand(
        [&sm](const std::string& messageType, const std::vector<std::byte>& /*payload*/) {
            if (messageType == "DrawDebugToggle") {
                LOG_INFO(Network, "Received DrawDebugToggle command from server");
                UiApi::DrawDebugToggle::Cwc cwc;
                sm.queueEvent(std::move(cwc));
            }
            else {
                LOG_WARN(Network, "Unknown server command: {}", messageType);
            }
        });

    // Setup binary callback for RenderMessage pushes from server.
    // Note: Command responses are routed via WebSocketService's pendingRequests_ map,
    // so this callback only receives RenderMessage payloads (already extracted from envelope).
    wsService.onBinary([&sm](const std::vector<std::byte>& bytes) {
        LOG_DEBUG(Network, "Received binary message ({} bytes)", bytes.size());

        try {
            // Deserialize as RenderMessageFull (includes scenario metadata).
            RenderMessageFull fullMsg;
            zpp::bits::in in(bytes);
            in(fullMsg).or_throw();

            const RenderMessage& renderMsg = fullMsg.render_data;

            // Reconstruct WorldData from RenderMessage.
            WorldData worldData;
            worldData.width = renderMsg.width;
            worldData.height = renderMsg.height;
            worldData.timestep = renderMsg.timestep;
            worldData.fps_server = renderMsg.fps_server;

            // Unpack cells based on format.
            size_t numCells = renderMsg.width * renderMsg.height;
            worldData.cells.resize(numCells);

            if (renderMsg.format == RenderFormat::DEBUG) {
                LOG_DEBUG(Network, "RenderMessage UNPACK: DEBUG format, {} cells", numCells);

                const DebugCell* debugCells =
                    reinterpret_cast<const DebugCell*>(renderMsg.payload.data());

                for (size_t i = 0; i < numCells; ++i) {
                    auto unpacked = RenderMessageUtils::unpackDebugCell(debugCells[i]);
                    worldData.cells[i].material_type = unpacked.material_type;
                    worldData.cells[i].fill_ratio = unpacked.fill_ratio;
                    worldData.cells[i].render_as = unpacked.render_as;
                    worldData.cells[i].com = unpacked.com;
                    worldData.cells[i].velocity = unpacked.velocity;
                    worldData.cells[i].pressure = unpacked.pressure_hydro;
                    worldData.cells[i].pressure_gradient = unpacked.pressure_gradient;
                }
            }
            else {
                // BASIC format: material + fill only.
                LOG_DEBUG(
                    Network,
                    "RenderMessage UNPACK: BASIC format, {} cells (no COM data)",
                    numCells);
                const BasicCell* basicCells =
                    reinterpret_cast<const BasicCell*>(renderMsg.payload.data());
                for (size_t i = 0; i < numCells; ++i) {
                    MaterialType material;
                    double fill_ratio;
                    int8_t render_as;
                    RenderMessageUtils::unpackBasicCell(basicCells[i], material, fill_ratio, render_as);
                    worldData.cells[i].material_type = material;
                    worldData.cells[i].fill_ratio = fill_ratio;
                    worldData.cells[i].render_as = render_as;
                }
            }

            // Apply sparse organism data.
            worldData.organism_ids = RenderMessageUtils::applyOrganismData(renderMsg.organisms, numCells);

            // Copy bone data for structural visualization.
            worldData.bones = renderMsg.bones;

            // Copy tree vision data if present.
            worldData.tree_vision = renderMsg.tree_vision;

            // Copy entities (duck, sparkle, etc.).
            worldData.entities = renderMsg.entities;

            // Create UiUpdateEvent and queue to EventSink.
            auto now = std::chrono::steady_clock::now();
            UiUpdateEvent evt{ .sequenceNum = 0,
                               .worldData = std::move(worldData),
                               .fps = 0,
                               .stepCount = static_cast<uint64_t>(renderMsg.timestep),
                               .isPaused = false,
                               .timestamp = now,
                               .scenario_id = fullMsg.scenario_id,
                               .scenario_config = fullMsg.scenario_config };

            sm.queueEvent(evt);
        }
        catch (const std::exception& e) {
            LOG_ERROR(Network, "Failed to process RenderMessage: {}", e.what());
        }
    });

    // NOW connect (after callbacks are registered).
    std::string url = "ws://" + cmd.host + ":" + std::to_string(cmd.port);
    auto connectResult = wsService.connect(url);
    if (connectResult.isError()) {
        LOG_ERROR(State, "WebSocketService connection failed: {}", connectResult.errorValue());

        // Track retry state - preserve current state but enable retries.
        retry_count_++;
        pending_host_ = cmd.host;
        pending_port_ = cmd.port;
        last_attempt_time_ = std::chrono::steady_clock::now();
        retry_pending_ = true;

        if (retry_count_ < MAX_RETRY_ATTEMPTS) {
            LOG_INFO(State, "Will retry connection in {:.0f}s (attempt {}/{})",
                RETRY_INTERVAL_SECONDS, retry_count_, MAX_RETRY_ATTEMPTS);
        }

        return std::move(*this);  // Stay in same state, preserving retry info.
    }

    // Connection initiated successfully - clear retry state.
    retry_pending_ = false;
    retry_count_ = 0;

    LOG_INFO(State, "WebSocketService connecting to {}", url);

    return StartMenu{};
}

State::Any Disconnected::onEvent(const ServerConnectedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Server connection established");
    LOG_INFO(State, "Transitioning to StartMenu");

    return StartMenu{};
}

State::Any Disconnected::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state.
    return Shutdown{};
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
