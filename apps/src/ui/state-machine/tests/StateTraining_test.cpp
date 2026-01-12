/**
 * @file StateTraining_test.cpp
 * @brief Unit tests for UI StateTraining.
 *
 * Tests the Training state which displays evolution progress and controls.
 * Written TDD-style - tests first, then implementation.
 */

#include "ui/state-machine/Event.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/state-machine/states/State.h"
#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Ui;
using namespace DirtSim::Ui::State;

/**
 * @brief Test fixture for Training state tests.
 *
 * Provides a test-mode StateMachine for state context.
 */
class StateTrainingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        stateMachine = std::make_unique<StateMachine>(StateMachine::TestMode{});
    }

    void TearDown() override { stateMachine.reset(); }

    std::unique_ptr<StateMachine> stateMachine;
};

/**
 * @brief Test that TrainButtonClicked transitions StartMenu to Training.
 */
TEST_F(StateTrainingTest, TrainButtonClickedTransitionsStartMenuToTraining)
{
    // Setup: Create StartMenu state.
    StartMenu startMenuState;

    // Setup: Create TrainButtonClicked event.
    TrainButtonClickedEvent evt;

    // Execute: Send event to StartMenu state.
    State::Any newState = startMenuState.onEvent(evt, *stateMachine);

    // Verify: State transitioned to Training.
    ASSERT_TRUE(std::holds_alternative<Training>(newState.getVariant()))
        << "StartMenu + TrainButtonClicked should transition to Training";
}

/**
 * @brief Test that Exit command in Training transitions to Shutdown.
 */
TEST_F(StateTrainingTest, ExitCommandTransitionsToShutdown)
{
    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create Exit command with callback.
    bool callbackInvoked = false;
    UiApi::Exit::Command cmd;
    UiApi::Exit::Cwc cwc(cmd, [&](UiApi::Exit::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    // Execute: Send Exit command.
    State::Any newState = trainingState.onEvent(cwc, *stateMachine);

    // Verify: State transitioned to Shutdown.
    ASSERT_TRUE(std::holds_alternative<Shutdown>(newState.getVariant()))
        << "Training + Exit should transition to Shutdown";
    EXPECT_TRUE(callbackInvoked) << "Response callback should be invoked";
}

/**
 * @brief Test that Training state has correct name.
 */
TEST_F(StateTrainingTest, HasCorrectStateName)
{
    Training trainingState;
    EXPECT_STREQ(trainingState.name(), "Training");
}

/**
 * @brief Test that EvolutionProgress event updates Training state.
 */
TEST_F(StateTrainingTest, EvolutionProgressUpdatesState)
{
    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create EvolutionProgress event.
    EvolutionProgressReceivedEvent evt;
    evt.progress.generation = 5;
    evt.progress.maxGenerations = 100;
    evt.progress.currentEval = 10;
    evt.progress.populationSize = 50;
    evt.progress.bestFitnessThisGen = 2.5;
    evt.progress.bestFitnessAllTime = 3.0;
    evt.progress.averageFitness = 1.5;

    // Execute: Send event to Training state.
    State::Any result = trainingState.onEvent(evt, *stateMachine);

    // Verify: State did not transition (nullopt means stay).
    EXPECT_FALSE(result.getVariant().index() != 6)
        << "Training + EvolutionProgress should not transition";

    // Verify: Progress was captured.
    EXPECT_EQ(trainingState.progress.generation, 5);
    EXPECT_EQ(trainingState.progress.maxGenerations, 100);
    EXPECT_EQ(trainingState.progress.currentEval, 10);
    EXPECT_EQ(trainingState.progress.populationSize, 50);
    EXPECT_DOUBLE_EQ(trainingState.progress.bestFitnessThisGen, 2.5);
    EXPECT_DOUBLE_EQ(trainingState.progress.bestFitnessAllTime, 3.0);
    EXPECT_DOUBLE_EQ(trainingState.progress.averageFitness, 1.5);
}

/**
 * @brief Test that ServerDisconnected transitions to Disconnected.
 */
TEST_F(StateTrainingTest, ServerDisconnectedTransitionsToDisconnected)
{
    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create disconnect event.
    ServerDisconnectedEvent evt{ "Connection lost" };

    // Execute: Send event to Training state.
    State::Any newState = trainingState.onEvent(evt, *stateMachine);

    // Verify: State transitioned to Disconnected.
    ASSERT_TRUE(std::holds_alternative<Disconnected>(newState.getVariant()))
        << "Training + ServerDisconnected should transition to Disconnected";
}

// TODO: Add tests requiring mock WebSocket:
// - onEnter sends EvolutionStart to server
// - Stop button sends EvolutionStop and transitions to StartMenu
