#include "State.h"
#include "core/LoggingChannels.h"
#include "core/RenderMessage.h"
#include "core/RenderMessageFull.h"
#include "core/RenderMessageUtils.h"
#include "core/ScenarioConfig.h"
#include "core/WorldData.h"
#include "core/api/UiUpdateEvent.h"
#include "core/network/WebSocketService.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/state-machine/network/MessageParser.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Ui {
namespace State {

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
                    RenderMessageUtils::unpackBasicCell(basicCells[i], material, fill_ratio);
                    worldData.cells[i].material_type = material;
                    worldData.cells[i].fill_ratio = fill_ratio;
                }
            }

            // Apply sparse organism data.
            auto organismIds = RenderMessageUtils::applyOrganismData(renderMsg.organisms, numCells);
            for (size_t i = 0; i < numCells; ++i) {
                worldData.cells[i].organism_id = organismIds[i];
            }

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
        return Disconnected{};
    }

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
