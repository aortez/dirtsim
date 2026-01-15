#include "State.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/World.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "server/ServerConfig.h"
#include "server/StateMachine.h"
#include "server/network/PeerDiscovery.h"

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

State::Any Idle::onEvent(const Api::EvolutionStart::Cwc& cwc, StateMachine& /*dsm*/)
{
    LOG_INFO(State, "EvolutionStart command received");

    Evolution newState;
    newState.evolutionConfig = cwc.command.evolution;
    newState.mutationConfig = cwc.command.mutation;
    newState.scenarioId = cwc.command.scenarioId;

    LOG_INFO(
        State,
        "Starting evolution: population={}, generations={}, scenario={}",
        cwc.command.evolution.populationSize,
        cwc.command.evolution.maxGenerations,
        toString(cwc.command.scenarioId));

    cwc.sendResponse(Api::EvolutionStart::Response::okay({ .started = true }));
    return newState;
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
    Scenario::EnumType scenarioId = cwc.command.scenario_id.has_value()
        ? cwc.command.scenario_id.value()
        : getScenarioId(dsm.serverConfig->startupConfig);
    LOG_INFO(State, "SimRun command received, using scenario '{}'", toString(scenarioId));

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
        LOG_ERROR(State, "Scenario '{}' not found in registry", toString(scenarioId));
        cwc.sendResponse(Api::SimRun::Response::error(
            ApiError(std::string("Scenario not found: ") + std::string(toString(scenarioId)))));
        return Idle{};
    }

    // Determine world dimensions: container-based > scenario requirements > defaults.
    uint32_t worldWidth = dsm.defaultWidth;
    uint32_t worldHeight = dsm.defaultHeight;

    if (cwc.command.container_size.x > 0 && cwc.command.container_size.y > 0) {
        constexpr int targetCellSize = 16;
        worldWidth = static_cast<uint32_t>(cwc.command.container_size.x / targetCellSize);
        worldHeight = static_cast<uint32_t>(cwc.command.container_size.y / targetCellSize);
        worldWidth = std::max(worldWidth, 10u);
        worldHeight = std::max(worldHeight, 10u);
    }
    else if (metadata->requiredWidth > 0 && metadata->requiredHeight > 0) {
        worldWidth = metadata->requiredWidth;
        worldHeight = metadata->requiredHeight;
    }

    // Create world with appropriate dimensions.
    LOG_INFO(
        State,
        "Creating World {}x{} (container: {}x{})",
        worldWidth,
        worldHeight,
        cwc.command.container_size.x,
        cwc.command.container_size.y);
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

    LOG_INFO(State, "Scenario '{}' applied to new world", toString(scenarioId));

    // Set run parameters.
    newState.stepDurationMs = cwc.command.timestep * 1000.0; // Convert seconds to milliseconds.
    newState.targetSteps =
        cwc.command.max_steps > 0 ? static_cast<uint32_t>(cwc.command.max_steps) : 0;
    newState.stepCount = 0;
    newState.frameLimit = cwc.command.max_frame_ms;

    spdlog::info(
        "Idle: World created, transitioning to {} (timestep={}ms, max_steps={}, "
        "max_frame_ms={})",
        cwc.command.start_paused ? "SimPaused" : "SimRunning",
        newState.stepDurationMs,
        cwc.command.max_steps,
        newState.frameLimit);

    // Send response: running=false if starting paused.
    cwc.sendResponse(Api::SimRun::Response::okay({ !cwc.command.start_paused, 0 }));

    // Transition to SimRunning or SimPaused based on start_paused flag.
    if (cwc.command.start_paused) {
        return SimPaused{ std::move(newState) };
    }
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
