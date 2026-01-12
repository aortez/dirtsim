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

// TODO: Add more tests as we implement Training features:
// - EvolutionProgress updates state
// - Stop button transitions to StartMenu
// - Pause/Resume behavior
// - ServerDisconnected transitions to Disconnected
