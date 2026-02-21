#include "State.h"
#include "core/Assert.h"
#include "core/Cell.h"
#include "core/ColorNames.h"
#include "core/GridOfCells.h"
#include "core/LightManager.h"
#include "core/LightTypes.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/WorldFrictionCalculator.h"
#include "core/input/GamepadManager.h"
#include "core/network/WebSocketService.h"
#include "core/organisms/Duck.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/PlayerDuckBrain.h"
#include "core/organisms/Tree.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/organisms/components/LightHandHeld.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/scenarios/ClockScenario.h"
#include "core/scenarios/NesScenario.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "server/ServerConfig.h"
#include "server/StateMachine.h"
#include "server/UserSettings.h"
#include "server/api/FingerDown.h"
#include "server/api/FingerMove.h"
#include "server/api/FingerUp.h"
#include "server/api/ScenarioSwitch.h"
#include "server/api/SimStop.h"
#include <chrono>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Server {
namespace State {

namespace {

Scenario::EnumType normalizeLegacyScenarioId(Scenario::EnumType scenarioId)
{
    if (scenarioId == Scenario::EnumType::DuckTraining) {
        return Scenario::EnumType::Clock;
    }
    return scenarioId;
}

constexpr float NES_ANALOG_DEADZONE = 0.25f;
constexpr uint8_t NES_BUTTON_A = 1u << 0;
constexpr uint8_t NES_BUTTON_B = 1u << 1;
constexpr uint8_t NES_BUTTON_SELECT = 1u << 2;
constexpr uint8_t NES_BUTTON_START = 1u << 3;
constexpr uint8_t NES_BUTTON_UP = 1u << 4;
constexpr uint8_t NES_BUTTON_DOWN = 1u << 5;
constexpr uint8_t NES_BUTTON_LEFT = 1u << 6;
constexpr uint8_t NES_BUTTON_RIGHT = 1u << 7;

uint8_t mapGamepadStateToNesButtons(const GamepadState& state)
{
    uint8_t result = 0;

    if (state.button_a) {
        result |= NES_BUTTON_A;
    }
    if (state.button_b) {
        result |= NES_BUTTON_B;
    }
    if (state.button_back) {
        result |= NES_BUTTON_SELECT;
    }
    if (state.button_start) {
        result |= NES_BUTTON_START;
    }

    const bool left = state.dpad_x < 0.0f || state.stick_x < -NES_ANALOG_DEADZONE;
    const bool right = state.dpad_x > 0.0f || state.stick_x > NES_ANALOG_DEADZONE;
    const bool up = state.dpad_y < 0.0f || state.stick_y < -NES_ANALOG_DEADZONE;
    const bool down = state.dpad_y > 0.0f || state.stick_y > NES_ANALOG_DEADZONE;

    if (up && !down) {
        result |= NES_BUTTON_UP;
    }
    if (down && !up) {
        result |= NES_BUTTON_DOWN;
    }
    if (left && !right) {
        result |= NES_BUTTON_LEFT;
    }
    if (right && !left) {
        result |= NES_BUTTON_RIGHT;
    }

    return result;
}

void applyUserClockTimezoneToConfig(ScenarioConfig& config, const UserSettings& userSettings)
{
    auto* clockConfig = std::get_if<Config::Clock>(&config);
    if (!clockConfig) {
        return;
    }

    clockConfig->timezoneIndex = static_cast<uint8_t>(std::clamp(
        userSettings.timezoneIndex, 0, static_cast<int>(ClockScenario::TIMEZONES.size()) - 1));
}

Vector2i resolveSeedPlacement(World& world, Vector2i requested)
{
    auto& data = world.getData();
    const int x = requested.x;
    const int y = requested.y;
    const int width = data.width;
    const int height = data.height;

    const auto isSpawnable = [&world, &data](int cellX, int cellY) {
        if (!data.inBounds(cellX, cellY)) {
            return false;
        }
        if (!data.at(cellX, cellY).isAir()) {
            return false;
        }
        return !world.getOrganismManager().hasOrganism({ cellX, cellY });
    };

    if (isSpawnable(x, y)) {
        return requested;
    }

    auto findNearestInRows = [&](int startY, int endY) -> std::optional<Vector2i> {
        if (startY > endY) {
            return std::nullopt;
        }

        long long bestDistance = std::numeric_limits<long long>::max();
        Vector2i best{ 0, 0 };
        bool found = false;

        for (int yy = startY; yy <= endY; ++yy) {
            for (int xx = 0; xx < width; ++xx) {
                if (!isSpawnable(xx, yy)) {
                    continue;
                }
                const long long dx = static_cast<long long>(xx) - x;
                const long long dy = static_cast<long long>(yy) - y;
                const long long distance = dx * dx + dy * dy;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = { xx, yy };
                    found = true;
                }
            }
        }

        if (!found) {
            return std::nullopt;
        }
        return best;
    };

    if (auto above = findNearestInRows(0, y); above.has_value()) {
        return above.value();
    }

    if (auto below = findNearestInRows(y + 1, height - 1); below.has_value()) {
        return below.value();
    }

    if (world.getOrganismManager().hasOrganism({ x, y })) {
        DIRTSIM_ASSERT(false, "SeedAdd: Spawn location already occupied");
    }

    data.at(x, y).clear();
    return requested;
}

void populateOrganismDebug(World& world, WorldData& data)
{
    data.organism_debug.clear();

    world.getOrganismManager().forEachOrganism([&](const Organism::Body& org) {
        WorldData::OrganismDebugInfo debug{
            .id = org.getId(),
            .type = "", // Set below based on type.
            .anchor_cell = org.getAnchorCell(),
            .material_at_anchor = "",                     // Set below.
            .organism_id_at_anchor = INVALID_ORGANISM_ID, // Set below.
            .genome_id = std::nullopt,
        };

        switch (org.getType()) {
            case OrganismType::DUCK:
                debug.type = "DUCK";
                break;
            case OrganismType::TREE:
                debug.type = "TREE";
                break;
            case OrganismType::GOOSE:
                debug.type = "GOOSE";
                break;
        }

        const WorldData& worldData = world.getData();
        if (worldData.inBounds(debug.anchor_cell.x, debug.anchor_cell.y)) {
            const Cell& cell = worldData.at(debug.anchor_cell.x, debug.anchor_cell.y);
            debug.material_at_anchor = toString(cell.material_type);
            debug.organism_id_at_anchor = world.getOrganismManager().at(debug.anchor_cell);
        }
        else {
            debug.material_at_anchor = "OUT_OF_BOUNDS";
        }

        if (debug.type == "TREE") {
            debug.genome_id = world.getOrganismManager().getGenomeId(org.getId());
        }

        data.organism_debug.push_back(std::move(debug));
    });
}
} // namespace

void SimRunning::onEnter(StateMachine& dsm)
{
    spdlog::info("SimRunning: Entering simulation state");

    // Create World if it doesn't exist (first time entering from Idle).
    if (!world) {
        spdlog::info("SimRunning: Creating new World {}x{}", dsm.defaultWidth, dsm.defaultHeight);
        world = std::make_unique<World>(dsm.defaultWidth, dsm.defaultHeight);
    }
    else {
        spdlog::info(
            "SimRunning: Resuming with existing World {}x{}",
            world->getData().width,
            world->getData().height);
    }

    // Apply default scenario if no scenario is set.
    if (world && scenario_id == Scenario::EnumType::Empty && dsm.serverConfig) {
        const Scenario::EnumType defaultScenarioId =
            normalizeLegacyScenarioId(dsm.getUserSettings().defaultScenario);
        spdlog::info("SimRunning: Applying default scenario '{}'", toString(defaultScenarioId));

        auto& registry = dsm.getScenarioRegistry();
        scenario = registry.createScenario(defaultScenarioId);

        if (scenario) {
            scenario_id = defaultScenarioId;

            // Clear world before applying scenario.
            for (int y = 0; y < world->getData().height; ++y) {
                for (int x = 0; x < world->getData().width; ++x) {
                    world->getData().at(x, y) = Cell();
                }
            }

            ScenarioConfig scenarioConfig = makeDefaultConfig(defaultScenarioId);
            if (getScenarioId(dsm.serverConfig->startupConfig) == defaultScenarioId) {
                scenarioConfig = dsm.serverConfig->startupConfig;
            }
            applyUserClockTimezoneToConfig(scenarioConfig, dsm.getUserSettings());
            scenario->setConfig(scenarioConfig, *world);

            // Run scenario setup to initialize world.
            scenario->setup(*world);

            // Register scenario with World for tick during advanceTime.
            world->setScenario(scenario.get());

            spdlog::info(
                "SimRunning: Default scenario '{}' applied with config",
                toString(defaultScenarioId));
        }
    }

    spdlog::info("SimRunning: Ready to run simulation (stepCount={})", stepCount);
}

void SimRunning::onExit(StateMachine& /*dsm. */)
{
    spdlog::info("SimRunning: Exiting state");
}

void SimRunning::tick(StateMachine& dsm)
{
    // Check if we've reached target steps.
    if (targetSteps > 0 && stepCount >= targetSteps) {
        spdlog::debug("SimRunning: Reached target steps ({}), not advancing", targetSteps);
        return;
    }

    // Headless server: advance physics simulation with fixed timestep accumulator.
    assert(world && "World must exist in SimRunning state");

    // Poll gamepad and manage player ducks.
    auto& gm = dsm.getGamepadManager();
    gm.poll();

    auto* nesScenario = (scenario_id == Scenario::EnumType::Nes)
        ? dynamic_cast<NesScenario*>(scenario.get())
        : nullptr;

    if (nesScenario != nullptr) {
        uint8_t controller1Buttons = 0;
        for (size_t i = 0; i < gm.getDeviceCount(); ++i) {
            const auto* state = gm.getGamepadState(i);
            if (!state || !state->connected) {
                continue;
            }
            controller1Buttons = mapGamepadStateToNesButtons(*state);
            break; // Use first connected gamepad for player one.
        }
        nesScenario->setController1State(controller1Buttons);
    }
    else {
        // Handle gamepad disconnects - remove ducks.
        for (size_t idx : gm.getNewlyDisconnected()) {
            auto it = gamepad_to_duck_.find(idx);
            if (it != gamepad_to_duck_.end()) {
                spdlog::info(
                    "SimRunning: Gamepad {} disconnected, removing duck {}", idx, it->second);
                world->getOrganismManager().removeOrganismFromWorld(*world, it->second);
                gamepad_to_duck_.erase(it);
            }
            prev_start_button_.erase(idx);
            prev_back_button_.erase(idx);
            prev_y_button_.erase(idx);
        }

        // Process each connected gamepad.
        for (size_t i = 0; i < gm.getDeviceCount(); i++) {
            auto* state = gm.getGamepadState(i);
            if (!state || !state->connected) {
                continue;
            }

            // Check for Start button press (edge-detected) to spawn duck.
            bool prev_start = prev_start_button_[i];
            if (state->button_start && !prev_start
                && gamepad_to_duck_.find(i) == gamepad_to_duck_.end()) {
                // Spawn a new player-controlled duck at center-top of world.
                uint32_t spawn_x = world->getData().width / 2;
                uint32_t spawn_y = 2;

                // Check if spawn location is occupied.
                Vector2i spawn_pos{ static_cast<int>(spawn_x), static_cast<int>(spawn_y) };
                OrganismId blocking = world->getOrganismManager().at(spawn_pos);
                if (blocking != INVALID_ORGANISM_ID) {
                    spdlog::warn(
                        "SimRunning: Gamepad {} spawn blocked by organism {} at ({}, {})",
                        i,
                        blocking,
                        spawn_x,
                        spawn_y);
                    continue;
                }

                auto brain = std::make_unique<PlayerDuckBrain>();
                OrganismId duck_id = world->getOrganismManager().createDuck(
                    *world, spawn_x, spawn_y, std::move(brain));

                gamepad_to_duck_[i] = duck_id;
                spdlog::info(
                    "SimRunning: Gamepad {} spawned duck {} at ({}, {})",
                    i,
                    duck_id,
                    spawn_x,
                    spawn_y);

                // Attach a handheld flashlight to the player-controlled duck.
                Duck* duck = world->getOrganismManager().getDuck(duck_id);
                if (duck) {
                    LightHandle flashlight = world->getLightManager().createLight(
                        SpotLight{ .position = Vector2d{ static_cast<double>(spawn_x),
                                                         static_cast<double>(spawn_y) },
                                   .color = ColorNames::warmSunlight(),
                                   .intensity = 1.0f,
                                   .radius = 15.0f,
                                   .attenuation = 0.1f,
                                   .direction = 0.0f,
                                   .arc_width = static_cast<float>(M_PI / 3.0),
                                   .focus = 1.0f });

                    auto handheld = std::make_unique<LightHandHeld>(std::move(flashlight));
                    duck->setHandheldLight(std::move(handheld));
                    spdlog::info("SimRunning: Attached flashlight to duck {}", duck_id);
                }
            }
            prev_start_button_[i] = state->button_start;

            // Check for Back/Select button press (edge-detected) to reset scenario.
            bool prev_back = prev_back_button_[i];
            if (state->button_back && !prev_back) {
                spdlog::info("SimRunning: Gamepad {} pressed Back, resetting scenario", i);
                if (scenario) {
                    scenario->reset(*world);
                    world->getData().tree_vision.reset();
                    world->getData().bones.clear();
                    gamepad_to_duck_.clear();
                    stepCount = 0;
                }
            }
            prev_back_button_[i] = state->button_back;

            // Check for Y button press (edge-detected) to toggle debug rendering.
            bool prev_y = prev_y_button_[i];
            if (state->button_y && !prev_y) {
                spdlog::info("SimRunning: Gamepad {} pressed Y, broadcasting DrawDebugToggle", i);
                dsm.broadcastCommand("DrawDebugToggle");
            }
            prev_y_button_[i] = state->button_y;

            // Pass gamepad input to existing duck's brain.
            auto it = gamepad_to_duck_.find(i);
            if (it != gamepad_to_duck_.end()) {
                if (auto* duck = world->getOrganismManager().getDuck(it->second)) {
                    if (auto* brain = duck->getBrain()) {
                        brain->setGamepadInput(*state);
                    }
                }
                else {
                    // Duck no longer exists (died, removed, etc.) - clean up mapping.
                    spdlog::debug(
                        "SimRunning: Gamepad {} duck {} no longer exists, cleaning up",
                        i,
                        it->second);
                    gamepad_to_duck_.erase(it);
                }
            }
        }
    }

    // Measure real elapsed time since last physics update.
    const auto now = std::chrono::steady_clock::now();

    // Advance physics by fixed timestep.
    // Note: Scenario tick is called inside World::advanceTime() after force clear.
    dsm.getTimers().startTimer("physics_step");
    world->advanceTime(FIXED_TIMESTEP_SECONDS);
    dsm.getTimers().stopTimer("physics_step");

    stepCount++;

    // HACK: Log flashlight state once per second for debugging.
    if (stepCount % 60 == 0) {
        for (const auto& [gamepad_idx, duck_id] : gamepad_to_duck_) {
            if (auto* duck = world->getOrganismManager().getDuck(duck_id)) {
                if (auto* light = duck->getHandheldLight()) {
                    spdlog::info(
                        "Flashlight [duck {}]: pitch={:.2f} rad, angVel={:.2f}, on={}",
                        duck_id,
                        light->getPitch(),
                        light->getAngularVelocity(),
                        light->isOn() ? "yes" : "no");
                }
            }
        }
    }

    // Calculate actual FPS (physics steps per second).
    const auto frameElapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - lastFrameTime).count();
    if (frameElapsed > 0) {
        actualFPS = 1000000.0 / frameElapsed;    // Microseconds to FPS.
        world->getData().fps_server = actualFPS; // Update WorldData for UI.
        lastFrameTime = now;

        // Log FPS and performance stats intermittently.
        if (stepCount == 100 || stepCount % 500 == 0) {
            spdlog::info("SimRunning: Actual FPS: {:.1f} (step {})", actualFPS, stepCount);

            // Log performance timing stats.
            auto& timers = dsm.getTimers();
            spdlog::info(
                "  Physics: {:.1f}ms avg ({} calls, {:.1f}ms total)",
                timers.getCallCount("physics_step") > 0 ? timers.getAccumulatedTime("physics_step")
                        / timers.getCallCount("physics_step")
                                                        : 0.0,
                timers.getCallCount("physics_step"),
                timers.getAccumulatedTime("physics_step"));
            spdlog::info(
                "  Cache update: {:.1f}ms avg ({} calls, {:.1f}ms total)",
                timers.getCallCount("cache_update") > 0 ? timers.getAccumulatedTime("cache_update")
                        / timers.getCallCount("cache_update")
                                                        : 0.0,
                timers.getCallCount("cache_update"),
                timers.getAccumulatedTime("cache_update"));
            spdlog::info(
                "  zpp_bits pack: {:.2f}ms avg ({} calls, {:.1f}ms total)",
                timers.getCallCount("serialize_worlddata") > 0
                    ? timers.getAccumulatedTime("serialize_worlddata")
                        / timers.getCallCount("serialize_worlddata")
                    : 0.0,
                timers.getCallCount("serialize_worlddata"),
                timers.getAccumulatedTime("serialize_worlddata"));
            spdlog::info(
                "  Network send: {:.2f}ms avg ({} calls, {:.1f}ms total)",
                timers.getCallCount("network_send") > 0 ? timers.getAccumulatedTime("network_send")
                        / timers.getCallCount("network_send")
                                                        : 0.0,
                timers.getCallCount("network_send"),
                timers.getAccumulatedTime("network_send"));
            spdlog::info(
                "  state_get immediate (total): {:.2f}ms avg ({} calls, {:.1f}ms total)",
                timers.getCallCount("state_get_immediate_total") > 0
                    ? timers.getAccumulatedTime("state_get_immediate_total")
                        / timers.getCallCount("state_get_immediate_total")
                    : 0.0,
                timers.getCallCount("state_get_immediate_total"),
                timers.getAccumulatedTime("state_get_immediate_total"));
        }
    }

    // Populate tree vision data (if any trees exist).
    Tree* firstTree = nullptr;
    world->getOrganismManager().forEachOrganism([&](Organism::Body& org) {
        if (!firstTree && org.getType() == OrganismType::TREE) {
            firstTree = static_cast<Tree*>(&org);
        }
    });

    if (firstTree) {
        // For now, show the first tree's vision (simple selection).
        world->getData().tree_vision = firstTree->gatherSensoryData(*world);

        if (stepCount % 100 == 0) {
            spdlog::info(
                "SimRunning: Tree vision active (tree_id={}, age_seconds={}, stage={})",
                firstTree->getId(),
                firstTree->getAge(),
                static_cast<int>(firstTree->getStage()));
        }
    }
    else {
        // No trees - clear tree vision.
        world->getData().tree_vision.reset();
    }

    // Update StateMachine's cached WorldData after all physics steps complete.
    dsm.getTimers().startTimer("cache_update");

    // INVARIANT CHECK: Entities must match organisms before caching.
    // Prevents stale entity sprites from being cached and served to clients.
    const WorldData& data = world->getData();
    size_t duck_organism_count = 0;
    size_t goose_organism_count = 0;
    world->getOrganismManager().forEachOrganism([&](const Organism::Body& org) {
        if (org.getType() == OrganismType::DUCK) duck_organism_count++;
        if (org.getType() == OrganismType::GOOSE) goose_organism_count++;
    });

    size_t duck_entity_count = 0;
    size_t goose_entity_count = 0;
    for (const auto& ent : data.entities) {
        if (ent.type == EntityType::Duck) duck_entity_count++;
        if (ent.type == EntityType::Goose) goose_entity_count++;
    }

    if (duck_organism_count != duck_entity_count) {
        spdlog::critical(
            "INVARIANT VIOLATION: {} duck organisms but {} duck entities!",
            duck_organism_count,
            duck_entity_count);
    }
    DIRTSIM_ASSERT(
        duck_organism_count == duck_entity_count,
        "Duck entities must match duck organisms before caching!");

    if (goose_organism_count != goose_entity_count) {
        spdlog::critical(
            "INVARIANT VIOLATION: {} goose organisms but {} goose entities!",
            goose_organism_count,
            goose_entity_count);
    }
    DIRTSIM_ASSERT(
        goose_organism_count == goose_entity_count,
        "Goose entities must match goose organisms before caching!");

    WorldData cachedData = world->getData();
    populateOrganismDebug(*world, cachedData);
    dsm.updateCachedWorldData(cachedData);
    dsm.getTimers().stopTimer("cache_update");

    spdlog::debug("SimRunning: Advanced simulation, total step {})", stepCount);

    // Send frame to UI clients after every physics update.
    if (dsm.getWebSocketService()) {
        auto& timers = dsm.getTimers();

        auto broadcastStart = std::chrono::steady_clock::now();
        timers.startTimer("broadcast_render_message");

        dsm.broadcastRenderMessage(
            world->getData(),
            world->getOrganismManager().getGrid(),
            scenario_id,
            scenario->getConfig());

        timers.stopTimer("broadcast_render_message");
        auto broadcastEnd = std::chrono::steady_clock::now();
        auto broadcastMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(broadcastEnd - broadcastStart)
                .count();

        static int sendCount = 0;
        static double totalBroadcastMs = 0.0;
        sendCount++;
        totalBroadcastMs += broadcastMs;
        if (sendCount % 1000 == 0) {
            spdlog::info(
                "Server: RenderMessage broadcast avg {:.1f}ms over {} frames (latest: {}ms, {} "
                "cells)",
                totalBroadcastMs / sendCount,
                sendCount,
                broadcastMs,
                world->getData().cells.size());
        }

        // Track FPS for frame send rate.
        auto now = std::chrono::steady_clock::now();
        if (lastFrameSendTime.time_since_epoch().count() > 0) {
            auto sendElapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(now - lastFrameSendTime)
                    .count();
            if (sendElapsed > 0) {
                frameSendFPS = 1000000.0 / sendElapsed;
                world->getData().fps_server = frameSendFPS; // Update WorldData for UI display.
            }
        }
        lastFrameSendTime = now;
    }
}

State::Any SimRunning::onEvent(const ApplyScenarioCommand& cmd, StateMachine& dsm)
{
    spdlog::info("SimRunning: Applying scenario: {}", toString(cmd.scenarioId));

    auto& registry = dsm.getScenarioRegistry();
    scenario = registry.createScenario(cmd.scenarioId);

    if (!scenario) {
        spdlog::error("Scenario not found: {}", toString(cmd.scenarioId));
        return std::move(*this);
    }

    if (world) {
        // Update scenario ID.
        scenario_id = cmd.scenarioId;

        // Register scenario with World for tick during advanceTime.
        world->setScenario(scenario.get());

        spdlog::info("SimRunning: Scenario '{}' applied", toString(cmd.scenarioId));
    }

    return std::move(*this);
}
State::Any SimRunning::onEvent(const Api::CellGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::CellGet::Response;

    // Validate coordinates.
    if (!world) {
        cwc.sendResponse(Response::error(ApiError("No world available")));
        return std::move(*this);
    }

    if (!world->getData().inBounds(cwc.command.x, cwc.command.y)) {
        cwc.sendResponse(Response::error(ApiError("Invalid coordinates")));
        return std::move(*this);
    }

    // Get cell.
    const Cell& cell = world->getData().at(cwc.command.x, cwc.command.y);

    cwc.sendResponse(Response::okay({ cell }));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::DiagramGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::DiagramGet::Response;

    assert(world && "World must exist in SimRunning state");

    std::string diagram;
    switch (cwc.command.style) {
        case Api::DiagramGet::DiagramStyle::Emoji:
            diagram = world->toAsciiDiagram();
            break;
        case Api::DiagramGet::DiagramStyle::Mixed:
            diagram = WorldDiagramGeneratorEmoji::generateMixedDiagram(*world);
            break;
        case Api::DiagramGet::DiagramStyle::Ansi:
            diagram =
                WorldDiagramGeneratorEmoji::generateAnsiDiagram(*world, cwc.command.useLitColors);
            break;
        default:
            diagram = world->toAsciiDiagram();
            break;
    }

    spdlog::info("DiagramGet: Generated diagram ({} bytes):\n{}", diagram.size(), diagram);

    cwc.sendResponse(Response::okay({ diagram }));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::CellSet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::CellSet::Response;

    // Validate world availability.
    if (!world) {
        cwc.sendResponse(Response::error(ApiError("No world available")));
        return std::move(*this);
    }

    // Validate coordinates.
    if (!world->getData().inBounds(cwc.command.x, cwc.command.y)) {
        cwc.sendResponse(Response::error(ApiError("Invalid coordinates")));
        return std::move(*this);
    }

    // Replace material (organism-aware). AIR clears the cell.
    world->replaceMaterialAtCell(
        { static_cast<int16_t>(cwc.command.x), static_cast<int16_t>(cwc.command.y) },
        cwc.command.material);

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::ClockEventTrigger::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::ClockEventTrigger::Response;

    if (!world || !scenario) {
        cwc.sendResponse(Response::error(ApiError("ClockEventTrigger requires an active world")));
        return std::move(*this);
    }

    auto* clockScenario = dynamic_cast<ClockScenario*>(scenario.get());
    if (!clockScenario) {
        cwc.sendResponse(Response::error(ApiError("ClockEventTrigger requires Clock scenario")));
        return std::move(*this);
    }

    if (!clockScenario->triggerEvent(*world, cwc.command.event_type)) {
        cwc.sendResponse(Response::error(ApiError("Clock event trigger rejected")));
        return std::move(*this);
    }

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::GravitySet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::GravitySet::Response;

    if (!world) {
        cwc.sendResponse(Response::error(ApiError("No world available")));
        return std::move(*this);
    }

    world->getPhysicsSettings().gravity = cwc.command.gravity;
    spdlog::info("SimRunning: API set gravity to {}", cwc.command.gravity);

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::PerfStatsGet::Cwc& cwc, StateMachine& dsm)
{
    using Response = Api::PerfStatsGet::Response;

    // Gather performance statistics from timers.
    auto& timers = dsm.getTimers();

    Api::PerfStatsGet::Okay stats;
    stats.fps = actualFPS;

    // Physics timing.
    stats.physics_calls = timers.getCallCount("physics_step");
    stats.physics_total_ms = timers.getAccumulatedTime("physics_step");
    stats.physics_avg_ms =
        stats.physics_calls > 0 ? stats.physics_total_ms / stats.physics_calls : 0.0;

    // Serialization timing.
    stats.serialization_calls = timers.getCallCount("serialize_worlddata");
    stats.serialization_total_ms = timers.getAccumulatedTime("serialize_worlddata");
    stats.serialization_avg_ms = stats.serialization_calls > 0
        ? stats.serialization_total_ms / stats.serialization_calls
        : 0.0;

    // Cache update timing.
    stats.cache_update_calls = timers.getCallCount("cache_update");
    stats.cache_update_total_ms = timers.getAccumulatedTime("cache_update");
    stats.cache_update_avg_ms =
        stats.cache_update_calls > 0 ? stats.cache_update_total_ms / stats.cache_update_calls : 0.0;

    // Network send timing.
    stats.network_send_calls = timers.getCallCount("network_send");
    stats.network_send_total_ms = timers.getAccumulatedTime("network_send");
    stats.network_send_avg_ms =
        stats.network_send_calls > 0 ? stats.network_send_total_ms / stats.network_send_calls : 0.0;

    spdlog::info(
        "SimRunning: API perf_stats_get returning {} physics steps, {} serializations",
        stats.physics_calls,
        stats.serialization_calls);

    cwc.sendResponse(Response::okay(std::move(stats)));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::TimerStatsGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::TimerStatsGet::Response;

    // Gather detailed timer statistics from World's timers.
    Api::TimerStatsGet::Okay stats;

    if (world) {
        auto timerNames = world->getTimers().getAllTimerNames();
        for (const auto& name : timerNames) {
            Api::TimerStatsGet::TimerEntry entry;
            entry.total_ms = world->getTimers().getAccumulatedTime(name);
            entry.calls = world->getTimers().getCallCount(name);
            entry.avg_ms = entry.calls > 0 ? entry.total_ms / entry.calls : 0.0;
            stats.timers[name] = entry;
        }
    }

    spdlog::info("SimRunning: API timer_stats_get returning {} timer entries", stats.timers.size());

    cwc.sendResponse(Response::okay(std::move(stats)));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::StatusGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::StatusGet::Response;

    if (!world) {
        cwc.sendResponse(Response::error(ApiError("No world available")));
        return std::move(*this);
    }

    // Return lightweight status (no cell data).
    Api::StatusGet::Okay status;
    status.timestep = stepCount;
    status.scenario_id = scenario_id;
    status.width = world->getData().width;
    status.height = world->getData().height;

    spdlog::debug(
        "SimRunning: API status_get (step {}, {}x{})",
        status.timestep,
        status.width,
        status.height);

    cwc.sendResponse(Response::okay(std::move(status)));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::Reset::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::Reset::Response;

    spdlog::info("SimRunning: API reset simulation");

    if (world && scenario) {
        // Reset scenario (clears world and reinitializes).
        scenario->reset(*world);

        // Clear tree vision and organism bone data from WorldData.
        world->getData().tree_vision.reset();
        world->getData().bones.clear();

        // Clear gamepad-controlled duck mappings (ducks are gone with reset).
        gamepad_to_duck_.clear();
    }

    stepCount = 0;

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::ScenarioConfigSet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::ScenarioConfigSet::Response;

    assert(world && "World must exist in SimRunning");
    assert(scenario && "Scenario must exist in SimRunning");

    // Update scenario's config (scenario is source of truth).
    scenario->setConfig(cwc.command.config, *world);

    LOG_INFO(State, "Scenario config updated for '{}'", toString(scenario_id));

    cwc.sendResponse(Response::okay({ true }));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::ScenarioSwitch::Cwc& cwc, StateMachine& dsm)
{
    using Response = Api::ScenarioSwitch::Response;

    assert(world && "World must exist in SimRunning");

    Scenario::EnumType newScenarioId = normalizeLegacyScenarioId(cwc.command.scenario_id);
    LOG_INFO(
        State,
        "Switching scenario from '{}' to '{}'",
        toString(scenario_id),
        toString(newScenarioId));

    // Create new scenario instance from registry.
    auto& registry = dsm.getScenarioRegistry();
    auto newScenario = registry.createScenario(newScenarioId);

    if (!newScenario) {
        LOG_ERROR(State, "Scenario '{}' not found in registry", toString(newScenarioId));
        cwc.sendResponse(
            Response::error(ApiError(
                std::string("Scenario not found: ") + std::string(toString(newScenarioId)))));
        return std::move(*this);
    }

    // Create fresh world for new scenario.
    const auto& metadata = newScenario->getMetadata();
    uint32_t newWidth = (metadata.requiredWidth > 0) ? metadata.requiredWidth : dsm.defaultWidth;
    uint32_t newHeight =
        (metadata.requiredHeight > 0) ? metadata.requiredHeight : dsm.defaultHeight;
    world = std::make_unique<World>(newWidth, newHeight);

    // Clear gamepad-controlled duck mappings (ducks are gone with the old world).
    gamepad_to_duck_.clear();

    // Get scenario's default config and apply it.
    ScenarioConfig defaultConfig = newScenario->getConfig();
    applyUserClockTimezoneToConfig(defaultConfig, dsm.getUserSettings());
    newScenario->setConfig(defaultConfig, *world);

    // Run scenario setup.
    newScenario->setup(*world);

    // Replace scenario and update ID.
    scenario = std::move(newScenario);
    scenario_id = newScenarioId;

    // Register scenario with World for tick during advanceTime.
    world->setScenario(scenario.get());

    // Reset step counter.
    stepCount = 0;

    LOG_INFO(State, "Switched to scenario '{}' successfully", toString(scenario_id));

    cwc.sendResponse(Response::okay({ true }));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::WorldResize::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::WorldResize::Response;

    const auto& cmd = cwc.command;
    spdlog::info("SimRunning: API resize world to {}x{}", cmd.width, cmd.height);

    if (world) {
        // Resize the world grid.
        world->resizeGrid(cmd.width, cmd.height);
        spdlog::debug("SimRunning: World resized successfully");
    }
    else {
        spdlog::error("SimRunning: Cannot resize - world is null");
        cwc.sendResponse(Response::error(ApiError("World not initialized")));
        return std::move(*this);
    }

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::SeedAdd::Cwc& cwc, StateMachine& dsm)
{
    using Response = Api::SeedAdd::Response;

    // Validate coordinates.
    if (!world->getData().inBounds(cwc.command.x, cwc.command.y)) {
        cwc.sendResponse(Response::error(ApiError("Invalid coordinates")));
        return std::move(*this);
    }

    // Build brain from genome if provided.
    std::unique_ptr<TreeBrain> brain = nullptr;
    std::optional<GenomeId> loadedGenomeId;
    if (cwc.command.genome_id) {
        auto& repo = dsm.getGenomeRepository();
        GenomeId id = GenomeId::fromString(cwc.command.genome_id.value());
        auto genome = repo.get(id);
        if (genome) {
            brain = std::make_unique<NeuralNetBrain>(*genome);
            loadedGenomeId = id;
            spdlog::info(
                "SeedAdd: Using genome '{}' for tree brain", cwc.command.genome_id.value());
        }
        else {
            spdlog::warn(
                "SeedAdd: Genome '{}' not found, using default brain",
                cwc.command.genome_id.value());
        }
    }

    const Vector2i requested{ cwc.command.x, cwc.command.y };
    const Vector2i spawnCell = resolveSeedPlacement(*world, requested);
    if (spawnCell.x != requested.x || spawnCell.y != requested.y) {
        spdlog::info(
            "SeedAdd: Adjusted spawn from ({}, {}) to ({}, {})",
            requested.x,
            requested.y,
            spawnCell.x,
            spawnCell.y);
    }

    // Plant seed as tree organism.
    spdlog::info("SeedAdd: Planting seed at ({}, {})", spawnCell.x, spawnCell.y);
    OrganismId tree_id =
        world->getOrganismManager().createTree(*world, spawnCell.x, spawnCell.y, std::move(brain));
    if (loadedGenomeId.has_value()) {
        world->getOrganismManager().setGenomeId(tree_id, loadedGenomeId.value());
    }
    spdlog::info("SeedAdd: Created tree organism {}", tree_id);

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::SpawnDirtBall::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::SpawnDirtBall::Response;

    if (!world) {
        cwc.sendResponse(Response::error(ApiError("No world available")));
        return std::move(*this);
    }

    // Spawn a dirt ball at top center.
    int16_t centerX = static_cast<int16_t>(world->getData().width / 2);
    int16_t topY = 2; // Start at row 2 to avoid the very top edge.

    spdlog::info("SpawnDirtBall: Spawning dirt ball at ({}, {})", centerX, topY);

    // Spawn a ball of the currently selected material.
    // Radius is calculated automatically as 15% of world width.
    Material::EnumType selectedMaterial = world->getSelectedMaterial();
    world->spawnMaterialBall(selectedMaterial, { centerX, topY });

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::PhysicsSettingsGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using namespace Api::PhysicsSettingsGet;

    assert(world && "World must exist in SimRunning state");

    spdlog::info("PhysicsSettingsGet: Sending current physics settings");

    Okay okay;
    okay.settings = world->getPhysicsSettings();

    cwc.sendResponse(Response::okay(okay));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::PhysicsSettingsSet::Cwc& cwc, StateMachine& /*dsm*/)
{
    assert(world && "World must exist in SimRunning state");

    using Response = Api::PhysicsSettingsSet::Response;

    spdlog::info("PhysicsSettingsSet: Applying new physics settings");

    // Update world's physics settings (calculators read from this directly).
    world->getPhysicsSettings() = cwc.command.settings;

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::StateGet::Cwc& cwc, StateMachine& dsm)
{
    using Response = Api::StateGet::Response;

    // Track total server-side processing time.
    auto requestStart = std::chrono::steady_clock::now();

    if (!world) {
        cwc.sendResponse(Response::error(ApiError("No world available")));
        return std::move(*this);
    }

    // Return cached WorldData (fast - uses pre-cached copy, no World copy overhead!).
    auto cachedPtr = dsm.getCachedWorldData();
    Api::StateGet::Okay responseData;

    if (cachedPtr) {
        responseData.worldData = *cachedPtr;
    }
    else {
        // Fallback: cache not ready yet, copy from world.
        responseData.worldData = world->getData();
    }

    populateOrganismDebug(*world, responseData.worldData);

    cwc.sendResponse(Response::okay(std::move(responseData)));

    // Log server processing time for state_get requests (includes serialization + send).
    auto requestEnd = std::chrono::steady_clock::now();
    double processingMs =
        std::chrono::duration<double, std::milli>(requestEnd - requestStart).count();
    spdlog::trace("SimRunning: state_get processed in {:.2f}ms (server-side total)", processingMs);

    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::SimRun::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::SimRun::Response;

    assert(world && "World must exist in SimRunning state");

    // Validate max_frame_ms parameter.
    if (cwc.command.max_frame_ms < 0) {
        spdlog::error("SimRunning: Invalid max_frame_ms value: {}", cwc.command.max_frame_ms);
        cwc.sendResponse(
            Response::error(
                ApiError("max_frame_ms must be >= 0 (0 = unlimited, >0 = frame rate cap)")));
        return std::move(*this);
    }

    // Store run parameters.
    stepDurationMs = cwc.command.timestep * 1000.0; // Convert seconds to milliseconds.
    targetSteps = cwc.command.max_steps > 0 ? static_cast<uint32_t>(cwc.command.max_steps) : 0;
    frameLimit = cwc.command.max_frame_ms;

    spdlog::info(
        "SimRunning: Starting autonomous simulation (timestep={}ms, max_steps={}, max_frame_ms={})",
        stepDurationMs,
        cwc.command.max_steps,
        frameLimit);

    // Send response indicating simulation is running.
    cwc.sendResponse(Response::okay({ true, stepCount }));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::SimStop::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::SimStop::Response;

    spdlog::info("SimRunning: SimStop command received, stopping simulation (step {})", stepCount);

    // Send success response before transitioning.
    cwc.sendResponse(Response::okay(std::monostate{}));

    // Transition back to Idle state.
    // World and scenario are destroyed when SimRunning is replaced.
    return Idle{};
}

State::Any SimRunning::onEvent(const PauseCommand& /*cmd*/, StateMachine& /*dsm. */)
{
    spdlog::info("SimRunning: Pausing at step {}", stepCount);

    // Move the current state into SimPaused.
    return SimPaused{ std::move(*this) };
}

State::Any SimRunning::onEvent(const ResetSimulationCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    spdlog::info("SimRunning: Resetting simulation");

    if (world && scenario) {
        scenario->reset(*world);

        // Clear tree vision and organism bone data from WorldData.
        world->getData().tree_vision.reset();
        world->getData().bones.clear();
    }

    stepCount = 0;

    return std::move(*this); // Stay in SimRunning (move because unique_ptr).
}

State::Any SimRunning::onEvent(const MouseDownEvent& /*evt*/, StateMachine& /*dsm*/)
{
    spdlog::debug("SimRunning: Mouse events not handled by headless server");
    return std::move(*this);
}

State::Any SimRunning::onEvent(const MouseMoveEvent& /*evt*/, StateMachine& /*dsm*/)
{
    spdlog::debug("SimRunning: Mouse events not handled by headless server");
    return std::move(*this);
}

State::Any SimRunning::onEvent(const MouseUpEvent& /*evt*/, StateMachine& /*dsm*/)
{
    spdlog::debug("SimRunning: Mouse events not handled by headless server");
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SelectMaterialCommand& cmd, StateMachine& /*dsm*/)
{
    if (world) {
        world->setSelectedMaterial(cmd.material);
        spdlog::debug("SimRunning: Selected material {}", static_cast<int>(cmd.material));
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetTimescaleCommand& cmd, StateMachine& /*dsm*/)
{
    // Update world directly (source of truth).
    if (world) {
        world->getPhysicsSettings().timescale = cmd.timescale;
        spdlog::info("SimRunning: Set timescale to {}", cmd.timescale);
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetElasticityCommand& cmd, StateMachine& /*dsm*/)
{
    // Update world directly (source of truth).
    if (world) {
        world->getPhysicsSettings().elasticity = cmd.elasticity;
        spdlog::info("SimRunning: Set elasticity to {}", cmd.elasticity);
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetDynamicStrengthCommand& cmd, StateMachine& /*dsm*/)
{
    // Update world directly (source of truth).
    if (world) {
        world->getPhysicsSettings().pressure_dynamic_strength = cmd.strength;
        spdlog::info("SimRunning: Set dynamic strength to {:.1f}", cmd.strength);
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetGravityCommand& cmd, StateMachine& /*dsm*/)
{
    // Update world directly (source of truth).
    if (world) {
        world->getPhysicsSettings().gravity = cmd.gravity;
        spdlog::info("SimRunning: Set gravity to {}", cmd.gravity);
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetPressureScaleCommand& cmd, StateMachine& /*dsm*/)
{
    // Apply to world.
    if (world) {
        world->getPhysicsSettings().pressure_scale = cmd.scale;
    }

    spdlog::debug("SimRunning: Set pressure scale to {}", cmd.scale);
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetPressureScaleWorldBCommand& cmd, StateMachine& /*dsm*/)
{
    // Apply to world.
    if (world) {
        world->getPhysicsSettings().pressure_scale = cmd.scale;
    }

    spdlog::debug("SimRunning: Set World pressure scale to {}", cmd.scale);
    return std::move(*this);
}

// Obsolete individual strength commands removed - use PhysicsSettingsSet instead.
// These settings are now controlled via the unified PhysicsSettings API.

State::Any SimRunning::onEvent(const SetContactFrictionStrengthCommand& cmd, StateMachine& /*dsm*/)
{
    if (auto* worldPtr = dynamic_cast<World*>(world.get())) {
        worldPtr->getPhysicsSettings().friction_strength = cmd.strength;
        spdlog::info("SimRunning: Set contact friction strength to {}", cmd.strength);
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetCOMCohesionRangeCommand& cmd, StateMachine& /*dsm*/)
{
    if (world) {
        world->setCOMCohesionRange(cmd.range);
        spdlog::info("SimRunning: Set COM cohesion range to {}", cmd.range);
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetAirResistanceCommand& cmd, StateMachine& /*dsm*/)
{
    if (world) {
        world->setAirResistanceStrength(cmd.strength);
        spdlog::info("SimRunning: Set air resistance to {}", cmd.strength);
    }
    return std::move(*this);
}

// Obsolete toggle commands removed - use PhysicsSettingsSet API instead.

State::Any SimRunning::onEvent(
    const SetHydrostaticPressureStrengthCommand& cmd, StateMachine& /*dsm*/)
{
    if (world) {
        world->getPhysicsSettings().pressure_hydrostatic_strength = cmd.strength;
        spdlog::info("SimRunning: Set hydrostatic pressure strength to {}", cmd.strength);
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetDynamicPressureStrengthCommand& cmd, StateMachine& /*dsm*/)
{
    // Apply to world.
    if (world) {
        // TODO: Need to add setDynamicPressureStrength method to WorldInterface.
        // For now, suppress unused warning.
        (void)world;
    }

    spdlog::debug("SimRunning: Set dynamic pressure strength to {}", cmd.strength);
    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetRainRateCommand& cmd, StateMachine& /*dsm*/)
{
    if (world && scenario) {
        ScenarioConfig config = scenario->getConfig();

        // Update rainRate in whichever config variant supports it.
        if (auto* sandboxCfg = std::get_if<Config::Sandbox>(&config)) {
            sandboxCfg->rainRate = cmd.rate;
            scenario->setConfig(config, *world);
            spdlog::info("SimRunning: Set rain rate to {} (Config::Sandbox)", cmd.rate);
        }
        else if (auto* rainingCfg = std::get_if<Config::Raining>(&config)) {
            rainingCfg->rainRate = cmd.rate;
            scenario->setConfig(config, *world);
            spdlog::info("SimRunning: Set rain rate to {} (Config::Raining)", cmd.rate);
        }
        else {
            spdlog::warn("SimRunning: Current scenario does not support rainRate");
        }
    }
    return std::move(*this);
}

// Handle immediate events routed through push system.
State::Any SimRunning::onEvent(const GetFPSCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    spdlog::debug("SimRunning: GetFPSCommand not implemented in headless server");
    // TODO: Track FPS if needed for headless operation.
    return std::move(*this);
}

State::Any SimRunning::onEvent(const GetSimStatsCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    spdlog::debug("SimRunning: GetSimStatsCommand not implemented in headless server");
    // TODO: Return simulation statistics if needed.
    return std::move(*this);
}

State::Any SimRunning::onEvent(const ToggleCohesionForceCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    if (world) {
        bool newValue = !world->isCohesionComForceEnabled();
        world->setCohesionComForceEnabled(newValue);
        spdlog::info("SimRunning: Cohesion force now: {}", newValue);
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const ToggleTimeHistoryCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    if (world) {
        bool newValue = !world->isTimeReversalEnabled();
        world->enableTimeReversal(newValue);
        spdlog::info("SimRunning: Time history now: {}", newValue);
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const PrintAsciiDiagramCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    // Get the current world and print ASCII diagram.
    if (world) {
        std::string ascii_diagram = world->toAsciiDiagram();
        spdlog::info("Current world state (ASCII diagram):\n{}", ascii_diagram);
    }
    else {
        spdlog::warn("PrintAsciiDiagramCommand: No world available");
    }

    return std::move(*this);
}

State::Any SimRunning::onEvent(const SpawnDirtBallCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    // Get the current world and spawn a ball at top center.
    if (world) {
        // Calculate the top center position.
        int16_t centerX = static_cast<int16_t>(world->getData().width / 2);
        int16_t topY = 2; // Start at row 2 to avoid the very top edge.

        // Spawn a ball of the currently selected material.
        // Radius is calculated automatically as 15% of world width.
        Material::EnumType selectedMaterial = world->getSelectedMaterial();
        world->spawnMaterialBall(selectedMaterial, { centerX, topY });
    }
    else {
        spdlog::warn("SpawnDirtBallCommand: No world available");
    }

    return std::move(*this);
}

State::Any SimRunning::onEvent(const SetFragmentationCommand& cmd, StateMachine& /*dsm*/)
{
    if (world) {
        world->setDirtFragmentationFactor(cmd.factor);
        spdlog::info("SimRunning: Set fragmentation factor to {}", cmd.factor);
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const ToggleWaterColumnCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    if (world && scenario) {
        ScenarioConfig config = scenario->getConfig();

        // Toggle waterColumnEnabled in Config::Sandbox.
        if (auto* sandboxCfg = std::get_if<Config::Sandbox>(&config)) {
            sandboxCfg->waterColumnEnabled = !sandboxCfg->waterColumnEnabled;
            scenario->setConfig(config, *world);
            spdlog::info(
                "SimRunning: Water column toggled - now: {}", sandboxCfg->waterColumnEnabled);
        }
        else {
            spdlog::warn("SimRunning: Current scenario does not support water column toggle");
        }
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const ToggleLeftThrowCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    // Note: Left throw is not currently in Config::Sandbox - this command is deprecated.
    // Use ScenarioConfigSet API to modify scenario configs directly.
    spdlog::warn("SimRunning: ToggleLeftThrowCommand is deprecated - left throw not in config");
    return std::move(*this);
}

State::Any SimRunning::onEvent(const ToggleRightThrowCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    if (world && scenario) {
        ScenarioConfig config = scenario->getConfig();

        // Toggle rightThrowEnabled in Config::Sandbox.
        if (auto* sandboxCfg = std::get_if<Config::Sandbox>(&config)) {
            sandboxCfg->rightThrowEnabled = !sandboxCfg->rightThrowEnabled;
            scenario->setConfig(config, *world);
            spdlog::info(
                "SimRunning: Right throw toggled - now: {}", sandboxCfg->rightThrowEnabled);
        }
        else {
            spdlog::warn("SimRunning: Current scenario does not support right throw toggle");
        }
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const ToggleQuadrantCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    if (world && scenario) {
        ScenarioConfig config = scenario->getConfig();

        // Toggle quadrantEnabled in Config::Sandbox.
        if (auto* sandboxCfg = std::get_if<Config::Sandbox>(&config)) {
            sandboxCfg->quadrantEnabled = !sandboxCfg->quadrantEnabled;
            scenario->setConfig(config, *world);
            spdlog::info("SimRunning: Quadrant toggled - now: {}", sandboxCfg->quadrantEnabled);
        }
        else {
            spdlog::warn("SimRunning: Current scenario does not support quadrant toggle");
        }
    }
    return std::move(*this);
}

State::Any SimRunning::onEvent(const ToggleFrameLimitCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    // TODO: Need to add toggleFrameLimit method to World.
    spdlog::info("SimRunning: Toggle frame limit");
    return std::move(*this);
}

State::Any SimRunning::onEvent(const QuitApplicationCommand& /*cmd*/, StateMachine& /*dsm*/)
{
    spdlog::info("Server::SimRunning: Quit application requested");

    // TODO: Add CaptureScreenshotCommand that ui/StateMachine can handle.
    // Screenshots are UI concerns, not server concerns.

    // Transition to Shutdown state.
    return Shutdown{};
}

State::Any SimRunning::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    spdlog::info("SimRunning: Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state (Shutdown.onEnter will set shouldExit flag).
    return Shutdown{};
}

State::Any SimRunning::onEvent(const Api::FingerDown::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::FingerDown::Response;

    const auto& cmd = cwc.command;
    spdlog::info(
        "FingerDown: finger_id={}, pos=({:.2f}, {:.2f}), radius={:.2f}",
        cmd.finger_id,
        cmd.world_x,
        cmd.world_y,
        cmd.radius);

    // Create or update finger session.
    FingerSession session;
    session.last_position = Vector2d{ cmd.world_x, cmd.world_y };
    session.radius = cmd.radius;
    session.active = true;

    fingerSessions[cmd.finger_id] = session;

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::FingerMove::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::FingerMove::Response;

    const auto& cmd = cwc.command;

    // Look up finger session.
    auto it = fingerSessions.find(cmd.finger_id);
    if (it == fingerSessions.end() || !it->second.active) {
        spdlog::warn("FingerMove: No active session for finger_id={}", cmd.finger_id);
        cwc.sendResponse(Response::error(ApiError("No active finger session")));
        return std::move(*this);
    }

    if (!world) {
        cwc.sendResponse(Response::error(ApiError("No world available")));
        return std::move(*this);
    }

    FingerSession& session = it->second;
    Vector2d new_position{ cmd.world_x, cmd.world_y };
    Vector2d delta = new_position - session.last_position;

    // Only apply force if there's meaningful movement.
    double delta_magnitude = delta.magnitude();
    if (delta_magnitude > 0.01) {
        // Normalize direction and scale force by movement speed.
        Vector2d force_direction = delta.normalize();

        // Force magnitude scales with drag speed. Tune this constant.
        constexpr double FORCE_SCALE = 5.0;
        double force_magnitude = delta_magnitude * FORCE_SCALE;

        // Apply force to all cells within radius of the NEW position.
        // Use the finger position as center, push outward in drag direction.
        const auto& grid = world->getData();
        double radius = session.radius;

        // Calculate bounding box for cells to check.
        int min_x = static_cast<int>(std::floor(new_position.x - radius));
        int max_x = static_cast<int>(std::ceil(new_position.x + radius));
        int min_y = static_cast<int>(std::floor(new_position.y - radius));
        int max_y = static_cast<int>(std::ceil(new_position.y + radius));

        // Clamp to world bounds.
        min_x = std::max(0, min_x);
        max_x = std::min(grid.width - 1, max_x);
        min_y = std::max(0, min_y);
        max_y = std::min(grid.height - 1, max_y);

        int cells_affected = 0;
        for (int cy = min_y; cy <= max_y; ++cy) {
            for (int cx = min_x; cx <= max_x; ++cx) {
                // Calculate distance from finger center to cell center.
                Vector2d cell_center{ cx + 0.5, cy + 0.5 };
                Vector2d to_cell = cell_center - new_position;
                double distance = to_cell.magnitude();

                // Only affect cells within radius.
                if (distance <= radius) {
                    Cell& cell = world->getData().at(cx, cy);

                    // Skip empty cells and walls.
                    if (cell.isEmpty() || cell.isWall()) {
                        continue;
                    }

                    // Force falloff: stronger at center, weaker at edge.
                    double falloff = 1.0 - (distance / radius);
                    falloff = falloff * falloff; // Quadratic falloff for smoother feel.

                    // Apply force in the drag direction.
                    Vector2d force = force_direction * (force_magnitude * falloff);
                    cell.addPendingForce(force);
                    cells_affected++;
                }
            }
        }

        spdlog::debug(
            "FingerMove: finger_id={}, delta=({:.3f}, {:.3f}), affected {} cells",
            cmd.finger_id,
            delta.x,
            delta.y,
            cells_affected);
    }

    // Update session with new position.
    session.last_position = new_position;

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const Api::FingerUp::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::FingerUp::Response;

    const auto& cmd = cwc.command;
    spdlog::info("FingerUp: finger_id={}", cmd.finger_id);

    // Remove finger session.
    auto it = fingerSessions.find(cmd.finger_id);
    if (it != fingerSessions.end()) {
        fingerSessions.erase(it);
    }

    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

} // namespace State
} // namespace Server
} // namespace DirtSim
