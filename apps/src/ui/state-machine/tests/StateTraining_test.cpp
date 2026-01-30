/**
 * @file StateTraining_test.cpp
 * @brief Unit tests for UI StateTraining.
 *
 * Tests the Training state which displays evolution progress and controls.
 * Written TDD-style - tests first, then implementation.
 */

#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/TrainingStreamConfigSet.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/states/State.h"
#include "ui/state-machine/tests/TestStateMachineFixture.h"
#include <gtest/gtest.h>
#include <optional>

using namespace DirtSim;
using namespace DirtSim::Ui;
using namespace DirtSim::Ui::State;
using namespace DirtSim::Ui::Tests;

/**
 * @brief Test that TrainButtonClicked transitions StartMenu to Training.
 */
TEST(StateTrainingTest, TrainButtonClickedTransitionsStartMenuToTraining)
{
    TestStateMachineFixture fixture;

    // Setup: Create StartMenu state.
    StartMenu startMenuState;

    // Setup: Create TrainButtonClicked event.
    TrainButtonClickedEvent evt;

    // Execute: Send event to StartMenu state.
    State::Any newState = startMenuState.onEvent(evt, *fixture.stateMachine);

    // Verify: State transitioned to Training.
    ASSERT_TRUE(std::holds_alternative<Training>(newState.getVariant()))
        << "StartMenu + TrainButtonClicked should transition to Training";
}

/**
 * @brief Test that Exit command in Training transitions to Shutdown.
 */
TEST(StateTrainingTest, ExitCommandTransitionsToShutdown)
{
    TestStateMachineFixture fixture;

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
    State::Any newState = trainingState.onEvent(cwc, *fixture.stateMachine);

    // Verify: State transitioned to Shutdown.
    ASSERT_TRUE(std::holds_alternative<Shutdown>(newState.getVariant()))
        << "Training + Exit should transition to Shutdown";
    EXPECT_TRUE(callbackInvoked) << "Response callback should be invoked";
}

/**
 * @brief Test that Training state has correct name.
 */
TEST(StateTrainingTest, HasCorrectStateName)
{
    Training trainingState;
    EXPECT_STREQ(trainingState.name(), "Training");
}

/**
 * @brief Test that EvolutionProgress event updates Training state.
 */
TEST(StateTrainingTest, EvolutionProgressUpdatesState)
{
    TestStateMachineFixture fixture;

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
    State::Any result = trainingState.onEvent(evt, *fixture.stateMachine);

    // Verify: State did not transition (stays in Training).
    EXPECT_TRUE(std::holds_alternative<Training>(result.getVariant()))
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
TEST(StateTrainingTest, ServerDisconnectedTransitionsToDisconnected)
{
    TestStateMachineFixture fixture;

    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create disconnect event.
    ServerDisconnectedEvent evt{ "Connection lost" };

    // Execute: Send event to Training state.
    State::Any newState = trainingState.onEvent(evt, *fixture.stateMachine);

    // Verify: State transitioned to Disconnected.
    ASSERT_TRUE(std::holds_alternative<Disconnected>(newState.getVariant()))
        << "Training + ServerDisconnected should transition to Disconnected";
}

/**
 * @brief Test that StartEvolutionButtonClicked sends EvolutionStart command.
 */
TEST(StateTrainingTest, StartEvolutionSendsCommand)
{
    TestStateMachineFixture fixture;

    // Setup: Configure expected responses.
    fixture.mockWebSocketService->expectSuccess<Api::EvolutionStart::Command>({ .started = true });
    fixture.mockWebSocketService->expectSuccess<Api::TrainingStreamConfigSet::Command>(
        { .intervalMs = 0, .message = "OK" });
    fixture.mockWebSocketService->expectSuccess<Api::RenderFormatSet::Command>(
        { .active_format = RenderFormat::EnumType::Basic, .message = "OK" });

    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create StartEvolutionButtonClicked event with config.
    StartEvolutionButtonClickedEvent evt;
    evt.evolution.populationSize = 10;
    evt.evolution.maxGenerations = 5;
    evt.mutation.rate = 0.1;
    evt.training.scenarioId = Scenario::EnumType::TreeGermination;
    evt.training.organismType = OrganismType::TREE;

    // Execute: Send event to Training state.
    State::Any result = trainingState.onEvent(evt, *fixture.stateMachine);

    // Verify: State did not transition (stays in Training).
    EXPECT_TRUE(std::holds_alternative<Training>(result.getVariant()))
        << "Training + StartEvolutionButtonClicked should stay in Training";

    // Verify: EvolutionStart command was sent.
    ASSERT_GE(fixture.mockWebSocketService->sentCommands().size(), 1)
        << "Should send at least EvolutionStart command";
    EXPECT_EQ(fixture.mockWebSocketService->sentCommands()[0], "EvolutionStart");
    ASSERT_GE(fixture.mockWebSocketService->sentCommands().size(), 2)
        << "Should send TrainingStreamConfigSet command";
    EXPECT_EQ(fixture.mockWebSocketService->sentCommands()[1], "TrainingStreamConfigSet");
    ASSERT_GE(fixture.mockWebSocketService->sentCommands().size(), 3)
        << "Should send RenderFormatSet command";
    EXPECT_EQ(fixture.mockWebSocketService->sentCommands()[2], "RenderFormatSet");
}

/**
 * @brief Test that StopTrainingClicked sends EvolutionStop and transitions to StartMenu.
 */
TEST(StateTrainingTest, StopButtonSendsCommandAndTransitions)
{
    TestStateMachineFixture fixture;

    // Setup: Configure expected response.
    fixture.mockWebSocketService->expectSuccess<Api::EvolutionStop::Command>(std::monostate{});

    // Setup: Create Training state.
    Training trainingState;

    // Setup: Create StopTrainingClicked event.
    StopTrainingClickedEvent evt;

    // Execute: Send event to Training state.
    State::Any newState = trainingState.onEvent(evt, *fixture.stateMachine);

    // Verify: State transitioned to StartMenu.
    ASSERT_TRUE(std::holds_alternative<StartMenu>(newState.getVariant()))
        << "Training + StopTrainingClicked should transition to StartMenu";

    // Verify: EvolutionStop command was sent.
    ASSERT_EQ(fixture.mockWebSocketService->sentCommands().size(), 1)
        << "Should send EvolutionStop command";
    EXPECT_EQ(fixture.mockWebSocketService->sentCommands()[0], "EvolutionStop");
}

/**
 * @brief Test that QuitTrainingClicked sends EvolutionStop when running and transitions.
 */
TEST(StateTrainingTest, QuitButtonStopsWhenRunning)
{
    TestStateMachineFixture fixture;

    // Setup: Configure expected response.
    fixture.mockWebSocketService->expectSuccess<Api::EvolutionStart::Command>({ .started = true });
    fixture.mockWebSocketService->expectSuccess<Api::TrainingStreamConfigSet::Command>(
        { .intervalMs = 0, .message = "OK" });
    fixture.mockWebSocketService->expectSuccess<Api::RenderFormatSet::Command>(
        { .active_format = RenderFormat::EnumType::Basic, .message = "OK" });
    fixture.mockWebSocketService->expectSuccess<Api::EvolutionStop::Command>(std::monostate{});

    // Setup: Create Training state with running evolution.
    Training trainingState;
    trainingState.onEvent(
        StartEvolutionButtonClickedEvent{ .evolution = EvolutionConfig{},
                                          .mutation = MutationConfig{},
                                          .training = TrainingSpec{} },
        *fixture.stateMachine);
    fixture.mockWebSocketService->clearSentCommands();

    // Setup: Create QuitTrainingClicked event.
    QuitTrainingClickedEvent evt;

    // Execute: Send event to Training state.
    State::Any newState = trainingState.onEvent(evt, *fixture.stateMachine);

    // Verify: State transitioned to StartMenu.
    ASSERT_TRUE(std::holds_alternative<StartMenu>(newState.getVariant()))
        << "Training + QuitTrainingClicked should transition to StartMenu";

    // Verify: EvolutionStop command was sent.
    ASSERT_EQ(fixture.mockWebSocketService->sentCommands().size(), 1)
        << "Should send EvolutionStop command";
    EXPECT_EQ(fixture.mockWebSocketService->sentCommands()[0], "EvolutionStop");
}

/**
 * @brief Test that QuitTrainingClicked does not send EvolutionStop when idle.
 */
TEST(StateTrainingTest, QuitButtonSkipsStopWhenIdle)
{
    TestStateMachineFixture fixture;

    // Setup: Create Training state (idle).
    Training trainingState;

    // Setup: Create QuitTrainingClicked event.
    QuitTrainingClickedEvent evt;

    // Execute: Send event to Training state.
    State::Any newState = trainingState.onEvent(evt, *fixture.stateMachine);

    // Verify: State transitioned to StartMenu.
    ASSERT_TRUE(std::holds_alternative<StartMenu>(newState.getVariant()))
        << "Training + QuitTrainingClicked should transition to StartMenu";

    // Verify: No EvolutionStop command was sent.
    EXPECT_TRUE(fixture.mockWebSocketService->sentCommands().empty());
}

/**
 * @brief Test best snapshot capture detection logic.
 *
 * The TrainingView captures a snapshot when:
 * 1. Evaluation changes (currentEval differs OR generation changes with currentEval=0)
 * 2. Best fitness improved (bestFitnessAllTime increased)
 *
 * This test verifies the detection logic without requiring LVGL.
 */
TEST(BestSnapshotDetectionTest, DetectsNewBestOnEvalChange)
{
    // Tracking state (mirrors TrainingView member variables).
    int lastEval = -1;
    int lastGeneration = -1;
    double lastBestFitness = -1.0;

    auto shouldCapture = [&](const Api::EvolutionProgress& progress) {
        const bool evalChanged = (progress.currentEval != lastEval)
            || (progress.generation != lastGeneration && progress.currentEval == 0);
        const bool fitnessImproved = (progress.bestFitnessAllTime > lastBestFitness + 0.001);

        bool capture = evalChanged && fitnessImproved;

        // Update tracking state.
        lastEval = progress.currentEval;
        lastGeneration = progress.generation;
        lastBestFitness = progress.bestFitnessAllTime;

        return capture;
    };

    // Scenario: First evaluation completes with fitness 0.5.
    // Progress update arrives showing eval changed from 0 to 1, fitness improved from 0 to 0.5.
    Api::EvolutionProgress progress1{
        .generation = 0,
        .maxGenerations = 10,
        .currentEval = 1,
        .populationSize = 5,
        .bestFitnessThisGen = 0.5,
        .bestFitnessAllTime = 0.5,
        .averageFitness = 0.5,
    };

    // First update with improvement should capture.
    EXPECT_TRUE(shouldCapture(progress1))
        << "Should capture when first best is found (eval changed, fitness improved)";

    // Scenario: Second evaluation completes, no improvement.
    Api::EvolutionProgress progress2{
        .generation = 0,
        .currentEval = 2,
        .populationSize = 5,
        .bestFitnessAllTime = 0.5, // Same as before.
    };

    EXPECT_FALSE(shouldCapture(progress2)) << "Should NOT capture when fitness did not improve";

    // Scenario: Third evaluation completes with new best.
    Api::EvolutionProgress progress3{
        .generation = 0,
        .currentEval = 3,
        .populationSize = 5,
        .bestFitnessAllTime = 0.75, // Improved!
    };

    EXPECT_TRUE(shouldCapture(progress3))
        << "Should capture when new best found (eval changed, fitness improved)";

    // Scenario: Same eval, same fitness (mid-evaluation tick).
    Api::EvolutionProgress progress4{
        .generation = 0,
        .currentEval = 3, // Same eval.
        .populationSize = 5,
        .bestFitnessAllTime = 0.75, // Same fitness.
    };

    EXPECT_FALSE(shouldCapture(progress4))
        << "Should NOT capture on mid-evaluation tick (no eval change)";

    // Scenario: Generation rollover with new best.
    Api::EvolutionProgress progress5{
        .generation = 1,  // New generation.
        .currentEval = 0, // Reset to 0.
        .populationSize = 5,
        .bestFitnessAllTime = 0.8, // Improved!
    };

    EXPECT_TRUE(shouldCapture(progress5))
        << "Should capture on generation rollover with improvement";
}
