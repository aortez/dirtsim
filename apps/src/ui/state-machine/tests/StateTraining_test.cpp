/**
 * @file StateTraining_test.cpp
 * @brief Unit tests for UI StateTraining.
 *
 * Tests the Training state which displays evolution progress and controls.
 * Written TDD-style - tests first, then implementation.
 */

#include "core/UUID.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/TrainingResultSave.h"
#include "server/api/TrainingStreamConfigSet.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/states/State.h"
#include "ui/state-machine/tests/TestStateMachineFixture.h"
#include <gtest/gtest.h>
#include <lvgl.h>
#include <optional>

using namespace DirtSim;
using namespace DirtSim::Ui;
using namespace DirtSim::Ui::State;
using namespace DirtSim::Ui::Tests;

namespace {

struct LvglTestDisplay {
    LvglTestDisplay()
    {
        lv_init();
        display = lv_display_create(800, 600);
    }

    ~LvglTestDisplay()
    {
        if (display) {
            lv_display_delete(display);
        }
        lv_deinit();
    }

    lv_display_t* display = nullptr;
};

} // namespace

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
 * @brief Test that TrainingResultSave with restart clears modal and restarts evolution session.
 */
TEST(StateTrainingTest, TrainingResultSaveWithRestartClearsModalAndRestarts)
{
    LvglTestDisplay lvgl;
    TestStateMachineFixture fixture;

    fixture.stateMachine->uiManager_ = std::make_unique<UiComponentManager>(lvgl.display);
    fixture.stateMachine->uiManager_->setEventSink(fixture.stateMachine.get());

    Training trainingState;
    trainingState.onEnter(*fixture.stateMachine);

    Api::TrainingResult::Summary summary;
    summary.scenarioId = Scenario::EnumType::TreeGermination;
    summary.organismType = OrganismType::TREE;
    summary.populationSize = 1;
    summary.maxGenerations = 1;
    summary.completedGenerations = 1;
    summary.bestFitness = 1.0;
    summary.averageFitness = 1.0;
    summary.totalTrainingSeconds = 1.0;
    summary.primaryBrainKind = TrainingBrainKind::NeuralNet;
    summary.primaryPopulationCount = 1;
    summary.trainingSessionId = UUID::generate();

    Api::TrainingResult::Candidate candidate;
    candidate.id = UUID::generate();
    candidate.fitness = 1.0;
    candidate.brainKind = TrainingBrainKind::NeuralNet;
    candidate.brainVariant = std::nullopt;
    candidate.generation = 0;

    trainingState.view_->showTrainingResultModal(summary, { candidate });
    ASSERT_TRUE(trainingState.view_->isTrainingResultModalVisible());

    Api::TrainingResultSave::Okay saveOkay;
    saveOkay.savedCount = 1;
    saveOkay.discardedCount = 0;
    saveOkay.savedIds = { candidate.id };
    fixture.mockWebSocketService->expectSuccess<Api::TrainingResultSave::Command>(saveOkay);
    fixture.mockWebSocketService->expectSuccess<Api::TrainingStreamConfigSet::Command>(
        { .intervalMs = trainingState.streamIntervalMs_, .message = "OK" });
    fixture.mockWebSocketService->expectSuccess<Api::RenderFormatSet::Command>(
        { .active_format = RenderFormat::EnumType::Basic, .message = "OK" });
    fixture.mockWebSocketService->clearSentCommands();

    bool callbackInvoked = false;
    UiApi::TrainingResultSave::Command cmd;
    cmd.count = 1;
    cmd.restart = true;
    UiApi::TrainingResultSave::Cwc cwc(cmd, [&](UiApi::TrainingResultSave::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
        if (response.isValue()) {
            EXPECT_EQ(response.value().savedCount, 1);
            EXPECT_EQ(response.value().discardedCount, 0);
            EXPECT_EQ(response.value().savedIds.size(), 1u);
        }
    });

    State::Any newState = trainingState.onEvent(cwc, *fixture.stateMachine);

    auto* updatedState = std::get_if<Training>(&newState.getVariant());
    ASSERT_NE(updatedState, nullptr);
    EXPECT_TRUE(callbackInvoked);
    EXPECT_TRUE(updatedState->evolutionStarted_);
    ASSERT_TRUE(updatedState->view_ != nullptr);
    EXPECT_FALSE(updatedState->view_->isTrainingResultModalVisible());

    const auto& sentCommands = fixture.mockWebSocketService->sentCommands();
    ASSERT_GE(sentCommands.size(), 3u);
    EXPECT_EQ(sentCommands[0], "TrainingResultSave");
    EXPECT_EQ(sentCommands[1], "TrainingStreamConfigSet");
    EXPECT_EQ(sentCommands[2], "RenderFormatSet");

    updatedState->onExit(*fixture.stateMachine);
    fixture.stateMachine->uiManager_.reset();
}
