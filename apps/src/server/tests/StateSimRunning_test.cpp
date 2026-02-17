#include "core/Cell.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/organisms/OrganismManager.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "server/ServerConfig.h"
#include "server/states/Idle.h"
#include "server/states/Shutdown.h"
#include "server/states/SimRunning.h"
#include "server/states/State.h"
#include "server/tests/TestStateMachineFixture.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::State;
using namespace DirtSim::Server::Tests;

namespace {

SimRunning createSimRunningWithWorld(StateMachine& stateMachine)
{
    // Create Idle and transition to SimRunning.
    Idle idleState;
    Api::SimRun::Command cmd{ 0.016, 150, 0, std::nullopt, false, {} };
    Api::SimRun::Cwc cwc(cmd, [](auto&&) {});
    State::Any state = idleState.onEvent(cwc, stateMachine);

    SimRunning simRunning = std::move(std::get<SimRunning>(state.getVariant()));

    // Call onEnter to initialize scenario.
    simRunning.onEnter(stateMachine);

    return simRunning;
}

void applyCleanScenario(StateMachine& stateMachine, SimRunning& simRunning)
{
    Config::Sandbox cleanConfig;
    cleanConfig.quadrantEnabled = false;
    cleanConfig.waterColumnEnabled = false;
    cleanConfig.rightThrowEnabled = false;
    cleanConfig.rainRate = 0.0;

    Api::ScenarioConfigSet::Command cmd;
    cmd.config = cleanConfig;
    Api::ScenarioConfigSet::Cwc cwc(cmd, [](auto&&) {});

    State::Any newState = simRunning.onEvent(cwc, stateMachine);
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));
}

} // namespace

/**
 * @brief Test that onEnter applies default Sandbox scenario.
 */
TEST(StateSimRunningTest, OnEnter_AppliesDefaultScenario)
{
    TestStateMachineFixture fixture;

    // Setup: Create SimRunning state with Sandbox scenario (applied by Idle).
    Idle idleState;
    Api::SimRun::Command cmd{ 0.016, 100, 0, std::nullopt, false, {} };
    Api::SimRun::Cwc cwc(cmd, [](auto&&) {});
    State::Any state = idleState.onEvent(cwc, *fixture.stateMachine);
    SimRunning simRunning = std::move(std::get<SimRunning>(state.getVariant()));

    // Verify: World exists and scenario already applied by Idle.
    ASSERT_NE(simRunning.world, nullptr);
    EXPECT_EQ(simRunning.scenario_id, Scenario::EnumType::Sandbox) << "Scenario applied by Idle";

    // Execute: Call onEnter (should not change scenario since it's already set).
    simRunning.onEnter(*fixture.stateMachine);

    // Verify: Sandbox scenario is still applied.
    EXPECT_EQ(simRunning.scenario_id, Scenario::EnumType::Sandbox)
        << "Scenario should remain Sandbox";

    // Verify: Walls exist (basic scenario setup check).
    const Cell& topLeft = simRunning.world->getData().at(0, 0);
    const Cell& bottomRight = simRunning.world->getData().at(
        simRunning.world->getData().width - 1, simRunning.world->getData().height - 1);
    EXPECT_EQ(topLeft.material_type, Material::EnumType::Wall) << "Walls should be created";
    EXPECT_EQ(bottomRight.material_type, Material::EnumType::Wall) << "Walls should be created";
}

/**
 * @brief Test that tick() steps physics and dirt falls.
 */
TEST(StateSimRunningTest, AdvanceSimulation_StepsPhysicsAndDirtFalls)
{
    TestStateMachineFixture fixture;

    // Setup: Create initialized SimRunning with clean scenario (no features).
    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);
    applyCleanScenario(*fixture.stateMachine, simRunning);

    // Setup: Manually add dirt at top center.
    const uint32_t testX = 14;
    const uint32_t testY = 5;

    // Debug: Check world state before adding dirt.
    spdlog::info(
        "TEST: World dimensions: {}x{}",
        simRunning.world->getData().width,
        simRunning.world->getData().height);
    spdlog::info("TEST: Gravity: {}", simRunning.world->getPhysicsSettings().gravity);

    simRunning.world->getData().at(testX, testY).addDirt(1.0);

    // Verify initial state.
    const Cell& startCell = simRunning.world->getData().at(testX, testY);
    const Cell& cellBelow = simRunning.world->getData().at(testX, testY + 1);
    spdlog::info(
        "TEST: Start cell ({},{}) material={}, fill={}",
        testX,
        testY,
        static_cast<int>(startCell.material_type),
        startCell.fill_ratio);
    spdlog::info(
        "TEST: Cell below ({},{}) material={}, fill={}",
        testX,
        testY + 1,
        static_cast<int>(cellBelow.material_type),
        cellBelow.fill_ratio);

    EXPECT_EQ(startCell.material_type, Material::EnumType::Dirt)
        << "Should have dirt at starting position";
    EXPECT_GT(startCell.fill_ratio, 0.9) << "Dirt should be nearly full";
    EXPECT_LT(cellBelow.fill_ratio, 0.1) << "Cell below should be empty initially";

    // Execute: Advance simulation up to 200 frames, checking for dirt movement.
    bool dirtFell = false;
    for (int i = 0; i < 200; ++i) {
        simRunning.tick(*fixture.stateMachine);

        // Debug: Log first few steps.
        if (i < 5 || i % 20 == 0) {
            const Cell& current = simRunning.world->getData().at(testX, testY);
            const Cell& below = simRunning.world->getData().at(testX, testY + 1);
            spdlog::info(
                "TEST: Step {} - Cell({},{}) mat={} fill={:.2f} COM=({:.3f},{:.3f}) "
                "vel=({:.3f},{:.3f})",
                i + 1,
                testX,
                testY,
                static_cast<int>(current.material_type),
                current.fill_ratio,
                current.com.x,
                current.com.y,
                current.velocity.x,
                current.velocity.y);
            spdlog::info(
                "TEST: Step {} - Cell({},{}) mat={} fill={:.2f}",
                i + 1,
                testX,
                testY + 1,
                static_cast<int>(below.material_type),
                below.fill_ratio);
        }

        // Check if dirt has moved to cell below.
        const Cell& cellBelow = simRunning.world->getData().at(testX, testY + 1);
        if (cellBelow.material_type == Material::EnumType::Dirt && cellBelow.fill_ratio > 0.1) {
            dirtFell = true;
            spdlog::info("Dirt fell after {} steps", i + 1);
            break;
        }
    }

    // Verify: Dirt fell to the cell below within 200 frames.
    ASSERT_TRUE(dirtFell) << "Dirt should fall to next cell within 200 frames";
    const Cell& finalCellBelow = simRunning.world->getData().at(testX, testY + 1);
    EXPECT_EQ(finalCellBelow.material_type, Material::EnumType::Dirt)
        << "Cell below should have dirt";
    EXPECT_GT(finalCellBelow.fill_ratio, 0.1) << "Cell below should have dirt";
    EXPECT_GT(simRunning.stepCount, 0u) << "Step count should have increased";
}

/**
 * @brief Test that StateGet returns correct WorldData.
 */
TEST(StateSimRunningTest, StateGet_ReturnsWorldData)
{
    TestStateMachineFixture fixture;

    // Setup: Create initialized SimRunning.
    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);

    // Setup: Create StateGet command with callback to capture response.
    bool callbackInvoked = false;
    Api::StateGet::Response capturedResponse;

    Api::StateGet::Command cmd;
    Api::StateGet::Cwc cwc(cmd, [&](Api::StateGet::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    // Execute: Send StateGet command.
    State::Any newState = simRunning.onEvent(cwc, *fixture.stateMachine);

    // Verify: Stays in SimRunning.
    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()));

    // Move to new state (simRunning is now moved-from).
    SimRunning& updatedState = std::get<SimRunning>(newState.getVariant());

    // Verify: Callback was invoked with success.
    ASSERT_TRUE(callbackInvoked) << "StateGet callback should be invoked";
    ASSERT_TRUE(capturedResponse.isValue()) << "StateGet should return success";

    // Verify: WorldData has correct properties.
    const WorldData& worldData = capturedResponse.value().worldData;
    const auto scenario_id = getScenarioId(fixture.stateMachine->serverConfig->startupConfig);
    const auto* metadata = fixture.stateMachine->getScenarioRegistry().getMetadata(scenario_id);
    ASSERT_NE(metadata, nullptr);

    uint32_t expected_width = fixture.stateMachine->defaultWidth;
    uint32_t expected_height = fixture.stateMachine->defaultHeight;
    if (metadata->requiredWidth > 0 && metadata->requiredHeight > 0) {
        expected_width = metadata->requiredWidth;
        expected_height = metadata->requiredHeight;
    }

    EXPECT_EQ(worldData.width, expected_width);
    EXPECT_EQ(worldData.height, expected_height);
    // Scenario ID is now in SimRunning state, not WorldData.
    EXPECT_EQ(updatedState.scenario_id, Scenario::EnumType::Sandbox);
    EXPECT_EQ(worldData.timestep, updatedState.stepCount);
}

/**
 * @brief Test that ScenarioConfigSet toggles water column off and on.
 */
TEST(StateSimRunningTest, ScenarioConfigSet_TogglesWaterColumn)
{
    TestStateMachineFixture fixture;

    // Setup: Create initialized SimRunning (water column ON by default).
    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);

    // Verify: Water column initially exists (check a few cells).
    // Water column height = world.height / 3 = 28 / 3 = 9, so check y=5 (middle of column).
    const Cell& waterCell = simRunning.world->getData().at(3, 5);
    EXPECT_EQ(waterCell.material_type, Material::EnumType::Water)
        << "Water column should exist initially";
    EXPECT_GT(waterCell.fill_ratio, 0.5) << "Water column cells should be filled";

    // Execute: Toggle water column OFF.
    Config::Sandbox configOff;
    configOff.quadrantEnabled = true;     // Keep quadrant.
    configOff.waterColumnEnabled = false; // Turn off water column.
    configOff.rightThrowEnabled = false;
    configOff.rainRate = 0.0;

    bool callbackInvoked = false;
    Api::ScenarioConfigSet::Command cmdOff;
    cmdOff.config = configOff;
    Api::ScenarioConfigSet::Cwc cwcOff(cmdOff, [&](auto&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue()) << "ScenarioConfigSet should succeed";
    });

    State::Any stateAfterOff = simRunning.onEvent(cwcOff, *fixture.stateMachine);
    simRunning = std::move(std::get<SimRunning>(stateAfterOff.getVariant()));

    // Verify: Water column removed.
    ASSERT_TRUE(callbackInvoked) << "Callback should be invoked";
    for (uint32_t y = 0; y < 20; ++y) {
        for (uint32_t x = 1; x <= 5; ++x) {
            const Cell& cell = simRunning.world->getData().at(x, y);
            EXPECT_TRUE(cell.material_type != Material::EnumType::Water || cell.fill_ratio < 0.1)
                << "Water column cells should be cleared at (" << x << "," << y << ")";
        }
    }

    // Execute: Toggle water column back ON.
    callbackInvoked = false;
    Config::Sandbox configOn = configOff;
    configOn.waterColumnEnabled = true;

    Api::ScenarioConfigSet::Command cmdOn;
    cmdOn.config = configOn;
    Api::ScenarioConfigSet::Cwc cwcOn(cmdOn, [&](auto&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any stateAfterOn = simRunning.onEvent(cwcOn, *fixture.stateMachine);
    simRunning = std::move(std::get<SimRunning>(stateAfterOn.getVariant()));

    // Verify: Water column restored.
    ASSERT_TRUE(callbackInvoked);
    const Cell& restoredWaterCell = simRunning.world->getData().at(3, 5);
    EXPECT_EQ(restoredWaterCell.material_type, Material::EnumType::Water)
        << "Water column should be restored";
    EXPECT_GT(restoredWaterCell.fill_ratio, 0.9) << "Water should be nearly full";
}

/**
 * @brief Test that ScenarioConfigSet toggles dirt quadrant off and on.
 */
TEST(StateSimRunningTest, ScenarioConfigSet_TogglesDirtQuadrant)
{
    TestStateMachineFixture fixture;

    // Setup: Create initialized SimRunning (quadrant ON by default).
    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);

    // Verify: Dirt quadrant initially exists (check a cell in lower-right).
    uint32_t quadX = simRunning.world->getData().width - 5;
    uint32_t quadY = simRunning.world->getData().height - 5;
    const Cell& quadCell = simRunning.world->getData().at(quadX, quadY);
    EXPECT_EQ(quadCell.material_type, Material::EnumType::Dirt)
        << "Quadrant should exist initially";
    EXPECT_GT(quadCell.fill_ratio, 0.5) << "Quadrant cells should be filled";

    // Execute: Toggle quadrant OFF.
    Config::Sandbox configOff;
    configOff.quadrantEnabled = false; // Turn off quadrant.
    configOff.waterColumnEnabled = false;
    configOff.rightThrowEnabled = false;
    configOff.rainRate = 0.0;

    bool callbackInvoked = false;
    Api::ScenarioConfigSet::Command cmdOff;
    cmdOff.config = configOff;
    Api::ScenarioConfigSet::Cwc cwcOff(cmdOff, [&](auto&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any stateAfterOff = simRunning.onEvent(cwcOff, *fixture.stateMachine);
    simRunning = std::move(std::get<SimRunning>(stateAfterOff.getVariant()));

    // Verify: Quadrant removed.
    ASSERT_TRUE(callbackInvoked);
    const Cell& clearedCell = simRunning.world->getData().at(quadX, quadY);
    EXPECT_TRUE(
        clearedCell.material_type != Material::EnumType::Dirt || clearedCell.fill_ratio < 0.1)
        << "Quadrant should be cleared";

    // Execute: Toggle quadrant back ON.
    callbackInvoked = false;
    Config::Sandbox configOn = configOff;
    configOn.quadrantEnabled = true;

    Api::ScenarioConfigSet::Command cmdOn;
    cmdOn.config = configOn;
    Api::ScenarioConfigSet::Cwc cwcOn(cmdOn, [&](auto&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any stateAfterOn = simRunning.onEvent(cwcOn, *fixture.stateMachine);
    simRunning = std::move(std::get<SimRunning>(stateAfterOn.getVariant()));

    // Verify: Quadrant restored.
    ASSERT_TRUE(callbackInvoked);
    const Cell& restoredCell = simRunning.world->getData().at(quadX, quadY);
    EXPECT_EQ(restoredCell.material_type, Material::EnumType::Dirt)
        << "Quadrant should be restored";
    EXPECT_GT(restoredCell.fill_ratio, 0.9) << "Quadrant cells should be filled";
}

/**
 * @brief Test that Exit command transitions to Shutdown.
 */
TEST(StateSimRunningTest, Exit_TransitionsToShutdown)
{
    TestStateMachineFixture fixture;

    // Setup: Create initialized SimRunning.
    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);

    // Setup: Create Exit command with callback.
    bool callbackInvoked = false;
    Api::Exit::Command cmd;
    Api::Exit::Cwc cwc(cmd, [&](Api::Exit::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    // Execute: Send Exit command.
    State::Any newState = simRunning.onEvent(cwc, *fixture.stateMachine);

    // Verify: Transitioned to Shutdown.
    ASSERT_TRUE(std::holds_alternative<Shutdown>(newState.getVariant()))
        << "Exit should transition to Shutdown";
    ASSERT_TRUE(callbackInvoked) << "Exit callback should be invoked";
}

/**
 * @brief Test that SimRun updates run parameters without recreating world.
 */
TEST(StateSimRunningTest, SimRun_UpdatesRunParameters)
{
    TestStateMachineFixture fixture;

    // Setup: Create initialized SimRunning with initial parameters.
    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);
    EXPECT_EQ(simRunning.targetSteps, 150u);
    EXPECT_DOUBLE_EQ(simRunning.stepDurationMs, 16.0);

    // Advance a few steps to verify world isn't recreated.
    for (int i = 0; i < 5; ++i) {
        simRunning.tick(*fixture.stateMachine);
    }
    EXPECT_EQ(simRunning.stepCount, 5u);

    // Execute: Send SimRun with new parameters.
    bool callbackInvoked = false;
    Api::SimRun::Command cmd{ 0.032, 50, 0, std::nullopt, false, {} };
    Api::SimRun::Cwc cwc(cmd, [&](Api::SimRun::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = simRunning.onEvent(cwc, *fixture.stateMachine);
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));

    // Verify: Parameters updated but world preserved.
    ASSERT_TRUE(callbackInvoked);
    EXPECT_EQ(simRunning.targetSteps, 50u) << "Target steps should be updated";
    EXPECT_DOUBLE_EQ(simRunning.stepDurationMs, 32.0) << "Step duration should be updated";
    EXPECT_EQ(simRunning.stepCount, 5u) << "Step count should be preserved (world not recreated)";
    EXPECT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()))
        << "Should stay in SimRunning";
}

/**
 * @brief Test that SeedAdd command places SEED material at the specified coordinates.
 */
TEST(StateSimRunningTest, SeedAdd_PlacesSeedAtCoordinates)
{
    TestStateMachineFixture fixture;

    // Setup: Create initialized SimRunning with clean scenario.
    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);
    applyCleanScenario(*fixture.stateMachine, simRunning);

    // Setup: Choose test coordinates (world is 28x28, avoid walls at boundaries).
    const uint32_t testX = 14;
    const uint32_t testY = 14;

    // Verify: Cell is initially empty (AIR).
    const Cell& cellBefore = simRunning.world->getData().at(testX, testY);
    EXPECT_EQ(cellBefore.material_type, Material::EnumType::Air)
        << "Cell should be empty initially";
    EXPECT_LT(cellBefore.fill_ratio, 0.1) << "Cell should have minimal fill initially";

    // Execute: Send SeedAdd command.
    bool callbackInvoked = false;
    Api::SeedAdd::Command cmd{
        .x = static_cast<int>(testX),
        .y = static_cast<int>(testY),
        .genome_id = std::nullopt,
    };
    Api::SeedAdd::Cwc cwc(cmd, [&](Api::SeedAdd::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue()) << "SeedAdd should succeed";
    });

    State::Any newState = simRunning.onEvent(cwc, *fixture.stateMachine);

    // Verify: Stays in SimRunning.
    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()));
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));

    // Verify: Callback was invoked.
    ASSERT_TRUE(callbackInvoked) << "SeedAdd callback should be invoked";

    // Verify: Cell now contains SEED material.
    const Cell& cellAfter = simRunning.world->getData().at(testX, testY);
    EXPECT_EQ(cellAfter.material_type, Material::EnumType::Seed)
        << "Cell should contain SEED material";
    EXPECT_GT(cellAfter.fill_ratio, 0.9) << "Cell should be nearly full with SEED";

    spdlog::info(
        "TEST: Seed placed at ({},{}) - material={}, fill={:.2f}",
        testX,
        testY,
        static_cast<int>(cellAfter.material_type),
        cellAfter.fill_ratio);
}

/**
 * @brief Test that SeedAdd uses the nearest air cell in the top half (including the source row).
 */
TEST(StateSimRunningTest, SeedAdd_FallsBackToNearestAirInTopHalf)
{
    TestStateMachineFixture fixture;

    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);
    applyCleanScenario(*fixture.stateMachine, simRunning);

    const int testX = 14;
    const int testY = 14;

    auto& data = simRunning.world->getData();
    const int width = data.width;
    const int height = data.height;

    for (int y = 0; y <= testY; ++y) {
        for (int x = 0; x < width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0f);
        }
    }
    for (int y = testY + 1; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0f);
        }
    }

    const int expectedX = testX - 1;
    const int expectedY = testY - 1;
    const int fartherX = testX - 3;
    const int fartherY = testY;
    const int bottomX = testX;
    const int bottomY = testY + 1;

    data.at(expectedX, expectedY).clear();
    data.at(fartherX, fartherY).clear();
    data.at(bottomX, bottomY).clear();

    bool callbackInvoked = false;
    Api::SeedAdd::Command cmd{
        .x = testX,
        .y = testY,
        .genome_id = std::nullopt,
    };
    Api::SeedAdd::Cwc cwc(cmd, [&](Api::SeedAdd::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue()) << "SeedAdd should succeed";
    });

    State::Any newState = simRunning.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()));
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));

    ASSERT_TRUE(callbackInvoked) << "SeedAdd callback should be invoked";

    const Cell& cellTop = simRunning.world->getData().at(expectedX, expectedY);
    EXPECT_EQ(cellTop.material_type, Material::EnumType::Seed);
    EXPECT_GT(cellTop.fill_ratio, 0.9f);

    const Cell& cellBottom = simRunning.world->getData().at(bottomX, bottomY);
    EXPECT_TRUE(cellBottom.isAir());
}

/**
 * @brief Test that SeedAdd uses the bottom half when the top half is full.
 */
TEST(StateSimRunningTest, SeedAdd_FallsBackToBottomHalfWhenTopHalfIsFull)
{
    TestStateMachineFixture fixture;

    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);
    applyCleanScenario(*fixture.stateMachine, simRunning);

    const int testX = 14;
    const int testY = 14;

    auto& data = simRunning.world->getData();
    const int width = data.width;
    const int height = data.height;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0f);
        }
    }

    const int bottomX = testX + 1;
    const int bottomY = testY + 1;
    data.at(bottomX, bottomY).clear();

    bool callbackInvoked = false;
    Api::SeedAdd::Command cmd{
        .x = testX,
        .y = testY,
        .genome_id = std::nullopt,
    };
    Api::SeedAdd::Cwc cwc(cmd, [&](Api::SeedAdd::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue()) << "SeedAdd should succeed";
    });

    State::Any newState = simRunning.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()));
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));

    ASSERT_TRUE(callbackInvoked) << "SeedAdd callback should be invoked";

    const Cell& cellBottom = simRunning.world->getData().at(bottomX, bottomY);
    EXPECT_EQ(cellBottom.material_type, Material::EnumType::Seed);
    EXPECT_GT(cellBottom.fill_ratio, 0.9f);
}

/**
 * @brief Test that SeedAdd rejects invalid coordinates.
 */
TEST(StateSimRunningTest, SeedAdd_RejectsInvalidCoordinates)
{
    TestStateMachineFixture fixture;

    // Setup: Create initialized SimRunning.
    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);

    // Test negative coordinates.
    bool callbackInvoked = false;
    Api::SeedAdd::Command cmd{
        .x = -1,
        .y = 10,
        .genome_id = std::nullopt,
    };
    Api::SeedAdd::Cwc cwc(cmd, [&](Api::SeedAdd::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isError()) << "SeedAdd should fail for negative x";
        EXPECT_EQ(response.errorValue().message, "Invalid coordinates");
    });

    State::Any newState = simRunning.onEvent(cwc, *fixture.stateMachine);
    ASSERT_TRUE(callbackInvoked) << "Callback should be invoked for invalid coordinates";
    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()))
        << "Should stay in SimRunning";

    // Move to updated state.
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));

    // Test coordinates beyond world bounds.
    callbackInvoked = false;
    Api::SeedAdd::Command cmd2{
        .x = simRunning.world->getData().width + 10,
        .y = 10,
        .genome_id = std::nullopt,
    };
    Api::SeedAdd::Cwc cwc2(cmd2, [&](Api::SeedAdd::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isError()) << "SeedAdd should fail for out-of-bounds x";
        EXPECT_EQ(response.errorValue().message, "Invalid coordinates");
    });

    newState = simRunning.onEvent(cwc2, *fixture.stateMachine);
    ASSERT_TRUE(callbackInvoked) << "Callback should be invoked for out-of-bounds coordinates";
    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()))
        << "Should stay in SimRunning";
}

/**
 * @brief Test that WorldResize command resizes the world grid.
 */
TEST(StateSimRunningTest, WorldResize_ResizesWorldGrid)
{
    TestStateMachineFixture fixture;

    // Setup: Create initialized SimRunning state.
    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);

    // Get initial world size.
    const uint32_t initialWidth = simRunning.world->getData().width;
    const uint32_t initialHeight = simRunning.world->getData().height;

    EXPECT_GT(initialWidth, 0) << "Initial width should be positive";
    EXPECT_GT(initialHeight, 0) << "Initial height should be positive";

    // Execute: Resize world to 50x50.
    bool callbackInvoked = false;
    Api::WorldResize::Command cmd;
    cmd.width = 50;
    cmd.height = 50;
    Api::WorldResize::Cwc cwc(cmd, [&](Api::WorldResize::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue()) << "WorldResize should succeed";
    });

    State::Any newState = simRunning.onEvent(cwc, *fixture.stateMachine);

    // Verify: Still in SimRunning.
    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()))
        << "Should stay in SimRunning";
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));

    // Verify: World resized correctly.
    ASSERT_TRUE(callbackInvoked) << "Callback should be invoked";
    EXPECT_EQ(simRunning.world->getData().width, 50) << "World width should be resized to 50";
    EXPECT_EQ(simRunning.world->getData().height, 50) << "World height should be resized to 50";

    // Execute: Resize world to smaller size (10x10).
    callbackInvoked = false;
    Api::WorldResize::Command cmd2;
    cmd2.width = 10;
    cmd2.height = 10;
    Api::WorldResize::Cwc cwc2(cmd2, [&](Api::WorldResize::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue()) << "WorldResize should succeed for smaller size";
    });

    newState = simRunning.onEvent(cwc2, *fixture.stateMachine);

    // Verify: Still in SimRunning with smaller world.
    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()))
        << "Should stay in SimRunning";
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));

    ASSERT_TRUE(callbackInvoked) << "Callback should be invoked for resize";
    EXPECT_EQ(simRunning.world->getData().width, 10) << "World width should be resized to 10";
    EXPECT_EQ(simRunning.world->getData().height, 10) << "World height should be resized to 10";

    // Execute: Resize world to larger size (100x100).
    callbackInvoked = false;
    Api::WorldResize::Command cmd3;
    cmd3.width = 100;
    cmd3.height = 100;
    Api::WorldResize::Cwc cwc3(cmd3, [&](Api::WorldResize::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue()) << "WorldResize should succeed for larger size";
    });

    newState = simRunning.onEvent(cwc3, *fixture.stateMachine);

    // Verify: Still in SimRunning with larger world.
    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()))
        << "Should stay in SimRunning";
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));

    ASSERT_TRUE(callbackInvoked) << "Callback should be invoked for resize";
    EXPECT_EQ(simRunning.world->getData().width, 100) << "World width should be resized to 100";
    EXPECT_EQ(simRunning.world->getData().height, 100) << "World height should be resized to 100";
}

/**
 * @brief Test that ScenarioSwitch clears organisms from previous scenario.
 */
TEST(StateSimRunningTest, ScenarioSwitch_ClearsOrganisms)
{
    TestStateMachineFixture fixture;

    // Setup: Create initialized SimRunning with Sandbox scenario.
    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);
    applyCleanScenario(*fixture.stateMachine, simRunning);

    // Add a duck organism.
    const uint32_t duckX = 10;
    const uint32_t duckY = 10;
    OrganismId duckId =
        simRunning.world->getOrganismManager().createDuck(*simRunning.world, duckX, duckY);

    // Verify duck exists.
    ASSERT_NE(duckId, INVALID_ORGANISM_ID);
    EXPECT_EQ(simRunning.world->getOrganismManager().getOrganismCount(), 1u);
    EXPECT_EQ(simRunning.world->getData().at(duckX, duckY).material_type, Material::EnumType::Wood);

    // Execute: Switch to Benchmark scenario.
    bool callbackInvoked = false;
    Api::ScenarioSwitch::Command cmd;
    cmd.scenario_id = Scenario::EnumType::Benchmark;
    Api::ScenarioSwitch::Cwc cwc(cmd, [&](Api::ScenarioSwitch::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue()) << "ScenarioSwitch should succeed";
    });

    State::Any newState = simRunning.onEvent(cwc, *fixture.stateMachine);

    // Verify: Still in SimRunning.
    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()));
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));

    // Verify: Callback invoked and organisms cleared.
    ASSERT_TRUE(callbackInvoked);
    EXPECT_EQ(simRunning.scenario_id, Scenario::EnumType::Benchmark);
    EXPECT_EQ(simRunning.world->getOrganismManager().getOrganismCount(), 0u)
        << "Organisms should be cleared on scenario switch";
}

TEST(StateSimRunningTest, ScenarioSwitch_LegacyDuckTrainingMapsToClock)
{
    TestStateMachineFixture fixture;

    SimRunning simRunning = createSimRunningWithWorld(*fixture.stateMachine);
    applyCleanScenario(*fixture.stateMachine, simRunning);

    bool callbackInvoked = false;
    Api::ScenarioSwitch::Command cmd;
    cmd.scenario_id = Scenario::EnumType::DuckTraining;
    Api::ScenarioSwitch::Cwc cwc(cmd, [&](Api::ScenarioSwitch::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue()) << "ScenarioSwitch should succeed";
    });

    State::Any newState = simRunning.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()));
    simRunning = std::move(std::get<SimRunning>(newState.getVariant()));

    ASSERT_TRUE(callbackInvoked);
    EXPECT_EQ(simRunning.scenario_id, Scenario::EnumType::Clock);

    const ScenarioConfig config = simRunning.scenario->getConfig();
    const auto* clockConfig = std::get_if<Config::Clock>(&config);
    ASSERT_NE(clockConfig, nullptr);
}
