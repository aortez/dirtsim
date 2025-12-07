#include "State.h"
#include "core/RenderMessage.h"
#include "core/RenderMessageUtils.h"
#include "core/WorldData.h"
#include "core/api/UiUpdateEvent.h"
#include "core/network/WebSocketService.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/state-machine/network/MessageParser.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <zpp_bits.h>

namespace DirtSim {
namespace Ui {
namespace State {

void Disconnected::onEnter(StateMachine& /*sm*/)
{
    spdlog::info("Disconnected: Not connected to DSSM server");
    spdlog::info("Disconnected: Show connection UI (host/port input, connect button)");
    // TODO: Display connection UI using SimulatorUI.
}

void Disconnected::onExit(StateMachine& /*sm*/)
{
    spdlog::info("Disconnected: Exiting");
}

State::Any Disconnected::onEvent(const ConnectToServerCommand& cmd, StateMachine& sm)
{
    spdlog::info("Disconnected: Connect command received (host={}, port={})", cmd.host, cmd.port);

    auto* wsService = sm.getWebSocketService();
    if (wsService) {
        // Setup callbacks before connecting.
        wsService->onConnected([&sm]() {
            spdlog::info("WebSocketService: Connected to server");
            sm.queueEvent(ServerConnectedEvent{});
        });

        wsService->onDisconnected([&sm]() {
            spdlog::warn("WebSocketService: Disconnected from server");
            sm.queueEvent(ServerDisconnectedEvent{ "Connection closed" });
        });

        wsService->onError([&sm](const std::string& error) {
            spdlog::error("WebSocketService: Connection error: {}", error);
            sm.queueEvent(ServerDisconnectedEvent{ error });
        });

        // Setup binary callback for RenderMessage pushes from server.
        // Note: Command responses are routed via WebSocketService's pendingRequests_ map,
        // so this callback only receives RenderMessage payloads (already extracted from envelope).
        wsService->onBinary([&sm](const std::vector<std::byte>& bytes) {
            spdlog::info("Received binary message ({} bytes)", bytes.size());

            try {
                // Deserialize as RenderMessage.
                RenderMessage renderMsg;
                zpp::bits::in in(bytes);
                in(renderMsg).or_throw();

                // Reconstruct WorldData from RenderMessage.
                WorldData worldData;
                worldData.width = renderMsg.width;
                worldData.height = renderMsg.height;
                worldData.timestep = renderMsg.timestep;
                worldData.fps_server = renderMsg.fps_server;
                worldData.scenario_id = renderMsg.scenario_id;
                worldData.scenario_config = renderMsg.scenario_config;

                // Unpack cells based on format.
                size_t numCells = renderMsg.width * renderMsg.height;
                worldData.cells.resize(numCells);

                if (renderMsg.format == RenderFormat::DEBUG) {
                    spdlog::info("RenderMessage UNPACK: DEBUG format, {} cells", numCells);

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
                    spdlog::info(
                        "RenderMessage UNPACK: BASIC format, {} cells (no COM data)", numCells);
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
                auto organismIds =
                    RenderMessageUtils::applyOrganismData(renderMsg.organisms, numCells);
                for (size_t i = 0; i < numCells; ++i) {
                    worldData.cells[i].organism_id = organismIds[i];
                }

                // Copy bone data for structural visualization.
                worldData.bones = renderMsg.bones;

                // Copy tree vision data if present.
                worldData.tree_vision = renderMsg.tree_vision;

                // Create UiUpdateEvent and queue to EventSink.
                auto now = std::chrono::steady_clock::now();
                UiUpdateEvent evt{ .sequenceNum = 0,
                                   .worldData = std::move(worldData),
                                   .fps = 0,
                                   .stepCount = static_cast<uint64_t>(renderMsg.timestep),
                                   .isPaused = false,
                                   .timestamp = now };

                sm.queueEvent(evt);
            }
            catch (const std::exception& e) {
                spdlog::error("WebSocketService: Failed to process RenderMessage: {}", e.what());
            }
        });

        // NOW connect (after callbacks are registered).
        std::string url = "ws://" + cmd.host + ":" + std::to_string(cmd.port);
        auto connectResult = wsService->connect(url);
        if (connectResult.isError()) {
            spdlog::error(
                "Disconnected: WebSocketService connection failed: {}", connectResult.errorValue());
            return Disconnected{};
        }

        spdlog::info("Disconnected: WebSocketService connecting to {}", url);
    }

    // Stay in Disconnected state - will transition to StartMenu on ServerConnectedEvent.
    return Disconnected{};
}

State::Any Disconnected::onEvent(const ServerConnectedEvent& /*evt*/, StateMachine& /*sm*/)
{
    spdlog::info("Disconnected: Server connection established");
    spdlog::info("Disconnected: Transitioning to StartMenu");

    // Transition to StartMenu state (show simulation controls).
    return StartMenu{};
}

State::Any Disconnected::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    spdlog::info("Disconnected: Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state.
    return Shutdown{};
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
