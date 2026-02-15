/**
 * @file StateTraining_test.cpp
 * @brief Unit tests for UI Training states.
 */

#include "core/UUID.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/StatusGet.h"
#include "server/api/TrainingResultSave.h"
#include "server/api/TrainingStreamConfigSet.h"
#include "server/api/UserSettingsSet.h"
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

TEST(StateTrainingTest, TrainButtonClickedTransitionsStartMenuToTraining)
{
    TestStateMachineFixture fixture;

    StartMenu startMenuState;

    TrainButtonClickedEvent evt;

    State::Any newState = startMenuState.onEvent(evt, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<TrainingIdle>(newState.getVariant()))
        << "StartMenu + TrainButtonClicked should transition to TrainingIdle";
}

TEST(StateTrainingTest, ExitCommandTransitionsToShutdown)
{
    TestStateMachineFixture fixture;

    bool callbackInvoked = false;
    UiApi::Exit::Command cmd;
    UiApi::Exit::Cwc cwc(cmd, [&](UiApi::Exit::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    fixture.stateMachine->handleEvent(cwc);

    EXPECT_EQ(fixture.stateMachine->getCurrentStateName(), "Shutdown");
    EXPECT_TRUE(fixture.stateMachine->shouldExit());
    EXPECT_TRUE(callbackInvoked) << "Response callback should be invoked";
}

TEST(StateTrainingTest, HasCorrectStateName)
{
    TrainingIdle idle;
    TrainingActive active;
    TrainingUnsavedResult unsaved;

    EXPECT_STREQ(idle.name(), "TrainingIdle");
    EXPECT_STREQ(active.name(), "TrainingActive");
    EXPECT_STREQ(unsaved.name(), "TrainingUnsavedResult");
}

TEST(StateTrainingTest, EvolutionProgressUpdatesState)
{
    LvglTestDisplay lvgl;
    TestStateMachineFixture fixture;

    TrainingActive trainingState;

    fixture.stateMachine->uiManager_ = std::make_unique<UiComponentManager>(lvgl.display);
    fixture.stateMachine->uiManager_->setEventSink(fixture.stateMachine.get());

    fixture.mockWebSocketService->expectSuccess<Api::TrainingStreamConfigSet::Command>(
        { .intervalMs = fixture.stateMachine->getUserSettings().streamIntervalMs,
          .message = "OK" });
    fixture.mockWebSocketService->expectSuccess<Api::RenderFormatSet::Command>(
        { .active_format = RenderFormat::EnumType::Basic, .message = "OK" });

    trainingState.onEnter(*fixture.stateMachine);

    EvolutionProgressReceivedEvent evt;
    evt.progress.generation = 5;
    evt.progress.maxGenerations = 100;
    evt.progress.currentEval = 10;
    evt.progress.populationSize = 50;
    evt.progress.bestFitnessThisGen = 2.5;
    evt.progress.bestFitnessAllTime = 3.0;
    evt.progress.averageFitness = 1.5;
    evt.progress.activeParallelism = 4;
    evt.progress.cpuPercent = 48.5;

    State::Any result = trainingState.onEvent(evt, *fixture.stateMachine);

    EXPECT_TRUE(std::holds_alternative<TrainingActive>(result.getVariant()))
        << "TrainingActive + EvolutionProgress should not transition";

    EXPECT_EQ(trainingState.progress.generation, 5);
    EXPECT_EQ(trainingState.progress.maxGenerations, 100);
    EXPECT_EQ(trainingState.progress.currentEval, 10);
    EXPECT_EQ(trainingState.progress.populationSize, 50);
    EXPECT_DOUBLE_EQ(trainingState.progress.bestFitnessThisGen, 2.5);
    EXPECT_DOUBLE_EQ(trainingState.progress.bestFitnessAllTime, 3.0);
    EXPECT_DOUBLE_EQ(trainingState.progress.averageFitness, 1.5);
    EXPECT_EQ(trainingState.progress.activeParallelism, 4);
    EXPECT_DOUBLE_EQ(trainingState.progress.cpuPercent, 48.5);

    trainingState.view_.reset();
}

TEST(StateTrainingTest, ServerDisconnectedTransitionsToDisconnected)
{
    LvglTestDisplay lvgl;
    TestStateMachineFixture fixture;

    fixture.stateMachine->uiManager_ = std::make_unique<UiComponentManager>(lvgl.display);
    fixture.stateMachine->uiManager_->setEventSink(fixture.stateMachine.get());

    ServerDisconnectedEvent evt{ "Connection lost" };

    fixture.stateMachine->handleEvent(evt);

    EXPECT_EQ(fixture.stateMachine->getCurrentStateName(), "Disconnected");
}

TEST(StateTrainingTest, ConnectTransitionsToTrainingActiveWhenServerIsEvolving)
{
    TestStateMachineFixture fixture;

    fixture.mockWebSocketService->expectSuccess<Api::StatusGet::Command>(
        { .state = "Evolution",
          .error_message = "",
          .timestep = 0,
          .scenario_id = std::nullopt,
          .width = 0,
          .height = 0,
          .cpu_percent = 0.0,
          .memory_percent = 0.0 });

    Disconnected disconnectedState;
    State::Any newState = disconnectedState.onEvent(
        ConnectToServerCommand{ .host = "localhost", .port = 8080 }, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<TrainingActive>(newState.getVariant()));

    const auto& sentCommands = fixture.mockWebSocketService->sentCommands();
    ASSERT_EQ(sentCommands.size(), 1u);
    EXPECT_EQ(sentCommands[0], "StatusGet");
}

TEST(StateTrainingTest, ConnectTransitionsToStartMenuWhenServerIsNotEvolving)
{
    TestStateMachineFixture fixture;

    fixture.mockWebSocketService->expectSuccess<Api::StatusGet::Command>(
        { .state = "Idle",
          .error_message = "",
          .timestep = 0,
          .scenario_id = std::nullopt,
          .width = 0,
          .height = 0,
          .cpu_percent = 0.0,
          .memory_percent = 0.0 });

    Disconnected disconnectedState;
    State::Any newState = disconnectedState.onEvent(
        ConnectToServerCommand{ .host = "localhost", .port = 8080 }, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<StartMenu>(newState.getVariant()));

    const auto& sentCommands = fixture.mockWebSocketService->sentCommands();
    ASSERT_EQ(sentCommands.size(), 1u);
    EXPECT_EQ(sentCommands[0], "StatusGet");
}

TEST(StateTrainingTest, StartEvolutionSendsCommand)
{
    LvglTestDisplay lvgl;
    TestStateMachineFixture fixture;

    fixture.stateMachine->uiManager_ = std::make_unique<UiComponentManager>(lvgl.display);
    fixture.stateMachine->uiManager_->setEventSink(fixture.stateMachine.get());

    fixture.mockWebSocketService->expectSuccess<Api::TrainingStreamConfigSet::Command>(
        { .intervalMs = fixture.stateMachine->getUserSettings().streamIntervalMs,
          .message = "OK" });
    fixture.mockWebSocketService->expectSuccess<Api::RenderFormatSet::Command>(
        { .active_format = RenderFormat::EnumType::Basic, .message = "OK" });
    fixture.mockWebSocketService->expectSuccess<Api::EvolutionStart::Command>({ .started = true });
    fixture.mockWebSocketService->expectSuccess<Api::UserSettingsSet::Command>(
        { .settings = fixture.stateMachine->getServerUserSettings() });

    TrainingIdle trainingState;
    trainingState.onEnter(*fixture.stateMachine);

    StartEvolutionButtonClickedEvent evt;
    evt.evolution.populationSize = 10;
    evt.evolution.maxGenerations = 5;
    evt.mutation.rate = 0.1;
    evt.training.scenarioId = Scenario::EnumType::TreeGermination;
    evt.training.organismType = OrganismType::TREE;

    State::Any result = trainingState.onEvent(evt, *fixture.stateMachine);

    auto* activeState = std::get_if<TrainingActive>(&result.getVariant());
    ASSERT_NE(activeState, nullptr);

    activeState->onEnter(*fixture.stateMachine);

    // Stream setup happens in TrainingIdle (before EvolutionStart) to prevent a deadlock, then
    // again in TrainingActive::onEnter for the restart-from-unsaved-result path.
    const auto& sentCommands = fixture.mockWebSocketService->sentCommands();
    ASSERT_GE(sentCommands.size(), 6u);
    EXPECT_EQ(sentCommands[0], "TrainingStreamConfigSet");
    EXPECT_EQ(sentCommands[1], "RenderFormatSet");
    EXPECT_EQ(sentCommands[2], "EvolutionStart");
    EXPECT_EQ(sentCommands[3], "UserSettingsSet");
    EXPECT_EQ(sentCommands[4], "TrainingStreamConfigSet");
    EXPECT_EQ(sentCommands[5], "RenderFormatSet");

    trainingState.view_.reset();
    activeState->view_.reset();
    fixture.stateMachine->uiManager_.reset();
}

TEST(StateTrainingTest, StopButtonSendsCommandAndTransitions)
{
    TestStateMachineFixture fixture;

    fixture.mockWebSocketService->expectSuccess<Api::EvolutionStop::Command>(std::monostate{});

    TrainingActive trainingState;

    StopTrainingClickedEvent evt;

    State::Any newState = trainingState.onEvent(evt, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<StartMenu>(newState.getVariant()))
        << "TrainingActive + StopTrainingClicked should transition to StartMenu";

    ASSERT_EQ(fixture.mockWebSocketService->sentCommands().size(), 1)
        << "Should send EvolutionStop command";
    EXPECT_EQ(fixture.mockWebSocketService->sentCommands()[0], "EvolutionStop");
}

TEST(StateTrainingTest, QuitButtonStopsWhenRunning)
{
    TestStateMachineFixture fixture;

    fixture.mockWebSocketService->expectSuccess<Api::EvolutionStop::Command>(std::monostate{});

    TrainingActive trainingState;

    QuitTrainingClickedEvent evt;

    State::Any newState = trainingState.onEvent(evt, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<StartMenu>(newState.getVariant()))
        << "TrainingActive + QuitTrainingClicked should transition to StartMenu";

    ASSERT_EQ(fixture.mockWebSocketService->sentCommands().size(), 1)
        << "Should send EvolutionStop command";
    EXPECT_EQ(fixture.mockWebSocketService->sentCommands()[0], "EvolutionStop");
}

TEST(StateTrainingTest, QuitButtonSkipsStopWhenIdle)
{
    TestStateMachineFixture fixture;

    TrainingIdle trainingState;

    QuitTrainingClickedEvent evt;

    State::Any newState = trainingState.onEvent(evt, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<StartMenu>(newState.getVariant()))
        << "TrainingIdle + QuitTrainingClicked should transition to StartMenu";

    EXPECT_TRUE(fixture.mockWebSocketService->sentCommands().empty());
}

TEST(StateTrainingTest, TrainingResultSaveWithRestartClearsModalAndRestarts)
{
    LvglTestDisplay lvgl;
    TestStateMachineFixture fixture;

    fixture.stateMachine->uiManager_ = std::make_unique<UiComponentManager>(lvgl.display);
    fixture.stateMachine->uiManager_->setEventSink(fixture.stateMachine.get());

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

    TrainingUnsavedResult trainingState{
        TrainingSpec{}, false, summary, std::vector<Api::TrainingResult::Candidate>{ candidate }
    };
    trainingState.onEnter(*fixture.stateMachine);

    ASSERT_TRUE(trainingState.isTrainingResultModalVisible());

    Api::TrainingResultSave::Okay saveOkay;
    saveOkay.savedCount = 1;
    saveOkay.discardedCount = 0;
    saveOkay.savedIds = { candidate.id };
    fixture.mockWebSocketService->expectSuccess<Api::TrainingResultSave::Command>(saveOkay);
    fixture.mockWebSocketService->expectSuccess<Api::TrainingStreamConfigSet::Command>(
        { .intervalMs = fixture.stateMachine->getUserSettings().streamIntervalMs,
          .message = "OK" });
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

    auto* updatedState = std::get_if<TrainingActive>(&newState.getVariant());
    ASSERT_NE(updatedState, nullptr);
    EXPECT_TRUE(callbackInvoked);

    trainingState.view_.reset();
    updatedState->onEnter(*fixture.stateMachine);
    EXPECT_FALSE(updatedState->isTrainingResultModalVisible());

    const auto& sentCommands = fixture.mockWebSocketService->sentCommands();
    ASSERT_GE(sentCommands.size(), 3u);
    EXPECT_EQ(sentCommands[0], "TrainingResultSave");
    EXPECT_EQ(sentCommands[1], "TrainingStreamConfigSet");
    EXPECT_EQ(sentCommands[2], "RenderFormatSet");

    updatedState->view_.reset();
    fixture.stateMachine->uiManager_.reset();
}
