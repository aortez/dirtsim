#include "core/World.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "server/ServerConfig.h"
#include "server/states/Idle.h"
#include "server/states/Shutdown.h"
#include "server/states/SimRunning.h"
#include "server/states/State.h"
#include "server/tests/TestStateMachineFixture.h"
#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::State;
using namespace DirtSim::Server::Tests;

/**
 * @brief Test that SimRun command creates a World and transitions to SimRunning.
 */
TEST(StateIdleTest, SimRunCreatesWorldAndTransitionsToSimRunning)
{
    TestStateMachineFixture fixture;

    // Setup: Create Idle state.
    Idle idleState;

    // Setup: Create SimRun command with callback to capture response.
    bool callbackInvoked = false;
    Api::SimRun::Response capturedResponse;

    Api::SimRun::Command cmd;
    cmd.timestep = 0.016; // 60 FPS.
    cmd.max_steps = 100;

    Api::SimRun::Cwc cwc(cmd, [&](Api::SimRun::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    // Execute: Send SimRun command to Idle state.
    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    // Verify: State transitioned to SimRunning.
    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()))
        << "Idle + SimRun should transition to SimRunning";

    // Verify: SimRunning has valid World.
    SimRunning& simRunning = std::get<SimRunning>(newState.getVariant());
    ASSERT_NE(simRunning.world, nullptr) << "SimRunning should have a World";
    const auto scenario_id = getScenarioId(fixture.stateMachine->serverConfig->startupConfig);
    const auto* metadata = fixture.stateMachine->getScenarioRegistry().getMetadata(scenario_id);
    ASSERT_NE(metadata, nullptr);

    uint32_t expected_width = fixture.stateMachine->defaultWidth;
    uint32_t expected_height = fixture.stateMachine->defaultHeight;
    if (metadata->requiredWidth > 0 && metadata->requiredHeight > 0) {
        expected_width = metadata->requiredWidth;
        expected_height = metadata->requiredHeight;
    }

    EXPECT_EQ(simRunning.world->getData().width, expected_width);
    EXPECT_EQ(simRunning.world->getData().height, expected_height);

    // Verify: SimRunning has correct run parameters.
    EXPECT_EQ(simRunning.stepCount, 0u) << "Initial step count should be 0";
    EXPECT_EQ(simRunning.targetSteps, 100u) << "Target steps should match command";
    EXPECT_DOUBLE_EQ(simRunning.stepDurationMs, 16.0) << "Step duration should be 16ms";

    // Note: Scenario application and wall setup happen in SimRunning::onEnter(),
    // which should be tested in StateSimRunning_test.cpp.

    // Verify: Response callback was invoked.
    ASSERT_TRUE(callbackInvoked) << "Response callback should be invoked";
    ASSERT_TRUE(capturedResponse.isValue()) << "Response should be success";
    EXPECT_TRUE(capturedResponse.value().running) << "Response should indicate running";
    EXPECT_EQ(capturedResponse.value().current_step, 0u) << "Initial step number is 0";
}

/**
 * @brief Test that Exit command transitions to Shutdown.
 */
TEST(StateIdleTest, ExitCommandTransitionsToShutdown)
{
    TestStateMachineFixture fixture;

    // Setup: Create Idle state.
    Idle idleState;

    // Setup: Create Exit command with callback to capture response.
    bool callbackInvoked = false;
    Api::Exit::Response capturedResponse;

    Api::Exit::Command cmd;
    Api::Exit::Cwc cwc(cmd, [&](Api::Exit::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    // Execute: Send Exit command to Idle state.
    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    // Verify: State transitioned to Shutdown.
    ASSERT_TRUE(std::holds_alternative<Shutdown>(newState.getVariant()))
        << "Idle + Exit should transition to Shutdown";

    // Verify: Response callback was invoked.
    ASSERT_TRUE(callbackInvoked) << "Response callback should be invoked";
    ASSERT_TRUE(capturedResponse.isValue()) << "Response should be success";
}

TEST(StateIdleTest, SimRunContainerSizeOverridesScenarioRequiredDimensions)
{
    TestStateMachineFixture fixture;

    Idle idleState;

    bool callbackInvoked = false;
    Api::SimRun::Command cmd;
    cmd.timestep = 0.016;
    cmd.max_steps = -1;
    cmd.scenario_id = Scenario::EnumType::Clock;
    cmd.container_size = Vector2s{ 800, 480 };

    Api::SimRun::Cwc cwc(cmd, [&](Api::SimRun::Response&&) { callbackInvoked = true; });

    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<SimRunning>(newState.getVariant()));
    SimRunning& simRunning = std::get<SimRunning>(newState.getVariant());
    ASSERT_NE(simRunning.world, nullptr);

    EXPECT_EQ(simRunning.world->getData().width, 800 / 16);
    EXPECT_EQ(simRunning.world->getData().height, 480 / 16);

    ASSERT_TRUE(callbackInvoked);
}
