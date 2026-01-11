#include "State.h"
#include "core/LoggingChannels.h"
#include "core/Timers.h"
#include "core/World.h"
#include "server/ServerConfig.h"
#include "server/StateMachine.h"
#include "server/network/PeerDiscovery.h"
#include "server/scenarios/ScenarioRegistry.h"

namespace DirtSim {
namespace Server {
namespace State {

void Idle::onEnter(StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Server ready, waiting for commands (no active World)");
    // Note: World is owned by SimRunning state, not StateMachine.
}

void Idle::onExit(StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Exiting");
}

State::Any Idle::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state (Shutdown.onEnter will set shouldExit flag).
    return Shutdown{};
}

State::Any Idle::onEvent(const Api::SimRun::Cwc& cwc, StateMachine& dsm)
{
    assert(dsm.serverConfig && "serverConfig must be loaded");

    // Use scenario_id from command if provided, otherwise fall back to server config.
    std::string scenarioId = !cwc.command.scenario_id.empty()
        ? cwc.command.scenario_id
        : getScenarioId(dsm.serverConfig->startupConfig);
    LOG_INFO(State, "SimRun command received, using scenario '{}'", scenarioId);

    // Validate max_frame_ms parameter.
    if (cwc.command.max_frame_ms < 0) {
        LOG_ERROR(State, "Invalid max_frame_ms value: {}", cwc.command.max_frame_ms);
        cwc.sendResponse(Api::SimRun::Response::error(
            ApiError("max_frame_ms must be >= 0 (0 = unlimited, >0 = frame rate cap)")));
        return Idle{};
    }

    // Create new SimRunning state with world.
    SimRunning newState;

    // Get scenario metadata first to check for required dimensions.
    auto& registry = dsm.getScenarioRegistry();
    const ScenarioMetadata* metadata = registry.getMetadata(scenarioId);

    if (!metadata) {
        LOG_ERROR(State, "Scenario '{}' not found in registry", scenarioId);
        cwc.sendResponse(
            Api::SimRun::Response::error(ApiError("Scenario not found: " + scenarioId)));
        return Idle{};
    }

    // Use scenario's required dimensions if specified, otherwise use defaults.
    uint32_t worldWidth = metadata->requiredWidth > 0 ? metadata->requiredWidth : dsm.defaultWidth;
    uint32_t worldHeight =
        metadata->requiredHeight > 0 ? metadata->requiredHeight : dsm.defaultHeight;

    // Create world with appropriate dimensions.
    LOG_INFO(State, "Creating new World {}x{}", worldWidth, worldHeight);
    newState.world = std::make_unique<World>(worldWidth, worldHeight);

    // Create scenario instance from factory.
    newState.scenario = registry.createScenario(scenarioId);
    assert(newState.scenario && "Scenario factory failed after metadata check");

    // Set scenario ID in state.
    newState.scenario_id = scenarioId;

    // Apply config from server.json.
    newState.scenario->setConfig(dsm.serverConfig->startupConfig, *newState.world);

    // Run scenario setup to initialize world.
    newState.scenario->setup(*newState.world);

    // Register scenario with World for tick during advanceTime.
    newState.world->setScenario(newState.scenario.get());

    LOG_INFO(State, "Scenario '{}' applied to new world", scenarioId);

    // Set run parameters.
    newState.stepDurationMs = cwc.command.timestep * 1000.0; // Convert seconds to milliseconds.
    newState.targetSteps =
        cwc.command.max_steps > 0 ? static_cast<uint32_t>(cwc.command.max_steps) : 0;
    newState.stepCount = 0;
    newState.frameLimit = cwc.command.max_frame_ms;

    spdlog::info(
        "Idle: World created, transitioning to SimRunning (timestep={}ms, max_steps={}, "
        "max_frame_ms={})",
        newState.stepDurationMs,
        cwc.command.max_steps,
        newState.frameLimit);

    // Send response immediately (before transition).
    cwc.sendResponse(Api::SimRun::Response::okay({ true, 0 }));

    // Transition to SimRunning.
    return newState;
}

State::Any Idle::onEvent(const Api::PeersGet::Cwc& cwc, StateMachine& dsm)
{
    auto peers = dsm.getPeerDiscovery().getPeers();
    LOG_DEBUG(State, "PeersGet returning {} peers", peers.size());

    Api::PeersGet::Okay response;
    response.peers = std::move(peers);
    cwc.sendResponse(Api::PeersGet::Response::okay(std::move(response)));

    return Idle{};
}

} // namespace State
} // namespace Server
} // namespace DirtSim
