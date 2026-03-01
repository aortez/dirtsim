#include "server/states/ScenarioSessionUtils.h"

#include "core/LoggingChannels.h"
#include "core/World.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"
#include "server/StateMachine.h"
#include "server/states/SimRunning.h"

#include <algorithm>

namespace DirtSim::Server::State {

namespace {

Result<std::monostate, ApiError> startGridWorldScenario(
    StateMachine& dsm,
    SimRunning& state,
    Scenario::EnumType scenarioId,
    const ScenarioMetadata& metadata,
    const ScenarioConfig& scenarioConfig,
    const Vector2s& containerSize)
{
    uint32_t worldWidth = dsm.defaultWidth;
    uint32_t worldHeight = dsm.defaultHeight;

    if (containerSize.x > 0 && containerSize.y > 0) {
        constexpr int targetCellSize = 16;
        worldWidth = static_cast<uint32_t>(containerSize.x / targetCellSize);
        worldHeight = static_cast<uint32_t>(containerSize.y / targetCellSize);
        worldWidth = std::max(worldWidth, 10u);
        worldHeight = std::max(worldHeight, 10u);
    }
    else if (metadata.requiredWidth > 0 && metadata.requiredHeight > 0) {
        worldWidth = metadata.requiredWidth;
        worldHeight = metadata.requiredHeight;
    }

    LOG_INFO(
        State,
        "Creating World {}x{} (container: {}x{})",
        worldWidth,
        worldHeight,
        containerSize.x,
        containerSize.y);

    auto& registry = dsm.getScenarioRegistry();
    auto scenario = registry.createScenario(scenarioId);
    if (!scenario) {
        return Result<std::monostate, ApiError>::error(ApiError(
            std::string("Scenario factory returned null for: ")
            + std::string(toString(scenarioId))));
    }

    state.world = std::make_unique<World>(worldWidth, worldHeight);
    state.scenario = std::move(scenario);
    state.nes_driver.reset();
    state.scenario_id = scenarioId;

    state.scenario->setConfig(scenarioConfig, *state.world);
    state.scenario->setup(*state.world);
    state.world->setScenario(state.scenario.get());

    // Clear NES-only state.
    state.nes_controller1_override_.reset();
    state.nes_scenario_config = ScenarioConfig{};
    state.nes_world_data = WorldData{};
    state.fingerSessions.clear();

    return Result<std::monostate, ApiError>::okay(std::monostate{});
}

Result<std::monostate, ApiError> startNesScenario(
    StateMachine& dsm,
    SimRunning& state,
    Scenario::EnumType scenarioId,
    const ScenarioConfig& scenarioConfig)
{
    (void)dsm;

    auto driver = std::make_unique<NesSmolnesScenarioDriver>(scenarioId);
    auto setResult = driver->setConfig(scenarioConfig);
    if (setResult.isError()) {
        return Result<std::monostate, ApiError>::error(ApiError(setResult.errorValue()));
    }

    auto setupResult = driver->setup();
    if (setupResult.isError()) {
        return Result<std::monostate, ApiError>::error(ApiError(setupResult.errorValue()));
    }

    // Tear down GridWorld state.
    state.world.reset();
    state.scenario.reset();

    state.nes_driver = std::move(driver);
    state.nes_scenario_config = scenarioConfig;
    state.nes_world_data = WorldData{};
    state.nes_world_data.width = 256;
    state.nes_world_data.height = 240;
    state.nes_world_data.cells.clear();
    state.nes_world_data.colors.data.clear();
    state.nes_world_data.scenario_video_frame.reset();
    state.nes_world_data.entities.clear();
    state.nes_world_data.tree_vision.reset();

    state.scenario_id = scenarioId;

    // Clear GridWorld-only state.
    state.gamepad_to_duck_.clear();
    state.prev_start_button_.clear();
    state.prev_back_button_.clear();
    state.prev_y_button_.clear();
    state.fingerSessions.clear();

    return Result<std::monostate, ApiError>::okay(std::monostate{});
}

} // namespace

Result<std::monostate, ApiError> startScenarioSession(
    StateMachine& dsm,
    SimRunning& state,
    Scenario::EnumType scenarioId,
    const ScenarioConfig& scenarioConfig,
    const Vector2s& containerSize)
{
    auto& registry = dsm.getScenarioRegistry();
    const ScenarioMetadata* metadata = registry.getMetadata(scenarioId);
    if (!metadata) {
        return Result<std::monostate, ApiError>::error(
            ApiError(std::string("Scenario not found: ") + std::string(toString(scenarioId))));
    }

    if (metadata->kind == ScenarioKind::NesWorld) {
        return startNesScenario(dsm, state, scenarioId, scenarioConfig);
    }

    return startGridWorldScenario(dsm, state, scenarioId, *metadata, scenarioConfig, containerSize);
}

} // namespace DirtSim::Server::State
