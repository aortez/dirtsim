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
#include "server/api/UserSettingsPatch.h"
#include "server/api/UserSettingsSet.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/states/State.h"
#include "ui/state-machine/states/TrainingFitnessHistory.h"
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

TEST(StateTrainingTest, TrainingFitnessHistoryKeepsRollingWindow)
{
    TrainingFitnessHistory history;

    Api::EvolutionProgress p0;
    p0.totalTrainingSeconds = 0.0;
    p0.currentEval = 1;
    p0.averageFitness = 0.1;
    p0.bestFitnessAllTime = 0.2;
    history.append(p0);

    Api::EvolutionProgress p60;
    p60.totalTrainingSeconds = 60.0;
    p60.currentEval = 2;
    p60.averageFitness = 1.1;
    p60.bestFitnessAllTime = 1.2;
    history.append(p60);

    Api::EvolutionProgress p121;
    p121.totalTrainingSeconds = 121.0;
    p121.currentEval = 3;
    p121.averageFitness = 2.1;
    p121.bestFitnessAllTime = 2.2;
    history.append(p121);

    std::vector<float> average;
    std::vector<float> best;
    history.getSeries(10, average, best);

    ASSERT_EQ(average.size(), 2u);
    ASSERT_EQ(best.size(), 2u);
    EXPECT_FLOAT_EQ(average[0], 1.1f);
    EXPECT_FLOAT_EQ(average[1], 2.1f);
    EXPECT_FLOAT_EQ(best[0], 1.2f);
    EXPECT_FLOAT_EQ(best[1], 2.2f);
}

TEST(StateTrainingTest, TrainingFitnessHistoryDownsamplesSeries)
{
    TrainingFitnessHistory history;
    for (int i = 0; i < 10; ++i) {
        Api::EvolutionProgress progress;
        progress.totalTrainingSeconds = static_cast<double>(i);
        progress.currentEval = i + 1;
        progress.averageFitness = static_cast<double>(i);
        progress.bestFitnessAllTime = static_cast<double>(100 + i);
        history.append(progress);
    }

    std::vector<float> average;
    std::vector<float> best;
    history.getSeries(4, average, best);

    ASSERT_EQ(average.size(), 4u);
    ASSERT_EQ(best.size(), 4u);
    EXPECT_FLOAT_EQ(average[0], 0.0f);
    EXPECT_FLOAT_EQ(average[1], 3.0f);
    EXPECT_FLOAT_EQ(average[2], 6.0f);
    EXPECT_FLOAT_EQ(average[3], 9.0f);
    EXPECT_FLOAT_EQ(best[0], 100.0f);
    EXPECT_FLOAT_EQ(best[1], 103.0f);
    EXPECT_FLOAT_EQ(best[2], 106.0f);
    EXPECT_FLOAT_EQ(best[3], 109.0f);
}

TEST(StateTrainingTest, TrainingFitnessHistoryResetsOnTimestampRollback)
{
    TrainingFitnessHistory history;

    Api::EvolutionProgress p10;
    p10.totalTrainingSeconds = 10.0;
    p10.currentEval = 10;
    p10.averageFitness = 1.0;
    p10.bestFitnessAllTime = 2.0;
    history.append(p10);

    Api::EvolutionProgress p5;
    p5.totalTrainingSeconds = 5.0;
    p5.currentEval = 1;
    p5.averageFitness = 7.0;
    p5.bestFitnessAllTime = 8.0;
    history.append(p5);

    std::vector<float> average;
    std::vector<float> best;
    history.getSeries(10, average, best);

    ASSERT_EQ(average.size(), 1u);
    ASSERT_EQ(best.size(), 1u);
    EXPECT_FLOAT_EQ(average[0], 7.0f);
    EXPECT_FLOAT_EQ(best[0], 8.0f);
}

TEST(StateTrainingTest, TrainingFitnessHistorySkipsEvalZeroSamples)
{
    TrainingFitnessHistory history;

    Api::EvolutionProgress reset;
    reset.totalTrainingSeconds = 100.0;
    reset.currentEval = 0;
    reset.averageFitness = 0.0;
    reset.bestFitnessAllTime = 2.7;
    history.append(reset);

    Api::EvolutionProgress evalOne;
    evalOne.totalTrainingSeconds = 101.0;
    evalOne.currentEval = 1;
    evalOne.averageFitness = 1.9;
    evalOne.bestFitnessAllTime = 2.7;
    history.append(evalOne);

    std::vector<float> average;
    std::vector<float> best;
    history.getSeries(10, average, best);

    ASSERT_EQ(average.size(), 1u);
    ASSERT_EQ(best.size(), 1u);
    EXPECT_FLOAT_EQ(average[0], 1.9f);
    EXPECT_FLOAT_EQ(best[0], 2.7f);
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

TEST(StateTrainingTest, TrainingFitnessPlotAppendsOnRobustAndNonGenomeProgress)
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

    ASSERT_EQ(trainingState.plotBestSeries_.size(), 1u);
    EXPECT_FLOAT_EQ(trainingState.plotBestSeries_.back(), 0.0f);

    const auto dispatchProgress = [&trainingState,
                                   &fixture](const EvolutionProgressReceivedEvent& evt) {
        State::Any result = trainingState.onEvent(evt, *fixture.stateMachine);
        ASSERT_TRUE(std::holds_alternative<TrainingActive>(result.getVariant()));
        trainingState = std::move(std::get<TrainingActive>(result.getVariant()));
    };

    EvolutionProgressReceivedEvent p0;
    p0.progress.generation = 5;
    p0.progress.currentEval = 10;
    p0.progress.populationSize = 50;
    p0.progress.bestFitnessThisGen = 9.9;
    p0.progress.robustEvaluationCount = 0;
    dispatchProgress(p0);
    EXPECT_EQ(trainingState.plotBestSeries_.size(), 1u)
        << "Mid-generation non-robust progress should not append yet";

    EvolutionProgressReceivedEvent p0Complete;
    p0Complete.progress.generation = 5;
    p0Complete.progress.currentEval = 50;
    p0Complete.progress.populationSize = 50;
    p0Complete.progress.lastCompletedGeneration = 5;
    p0Complete.progress.bestThisGenSource = "seed";
    p0Complete.progress.bestFitnessThisGen = 9.9;
    p0Complete.progress.robustEvaluationCount = 0;
    dispatchProgress(p0Complete);
    ASSERT_EQ(trainingState.plotBestSeries_.size(), 2u);
    EXPECT_FLOAT_EQ(trainingState.plotBestSeries_.back(), 9.9f);

    EvolutionProgressReceivedEvent p0CompleteRepeat = p0Complete;
    p0CompleteRepeat.progress.bestFitnessThisGen = 8.8;
    dispatchProgress(p0CompleteRepeat);
    EXPECT_EQ(trainingState.plotBestSeries_.size(), 2u)
        << "Repeated completed generation should not append duplicate points";

    EvolutionProgressReceivedEvent p1;
    p1.progress.generation = 5;
    p1.progress.currentEval = 50;
    p1.progress.populationSize = 50;
    p1.progress.bestFitnessThisGen = 1.5;
    p1.progress.robustEvaluationCount = 1;
    dispatchProgress(p1);
    ASSERT_EQ(trainingState.plotBestSeries_.size(), 3u);
    EXPECT_FLOAT_EQ(trainingState.plotBestSeries_.back(), 1.5f);

    EvolutionProgressReceivedEvent p1Repeat;
    p1Repeat.progress.generation = 5;
    p1Repeat.progress.currentEval = 50;
    p1Repeat.progress.populationSize = 50;
    p1Repeat.progress.bestFitnessThisGen = 1.4;
    p1Repeat.progress.robustEvaluationCount = 1;
    dispatchProgress(p1Repeat);
    EXPECT_EQ(trainingState.plotBestSeries_.size(), 3u)
        << "Repeated robust evaluation count should not append duplicate points";

    EvolutionProgressReceivedEvent p2;
    p2.progress.generation = 6;
    p2.progress.currentEval = 50;
    p2.progress.populationSize = 50;
    p2.progress.bestFitnessThisGen = 0.8;
    p2.progress.robustEvaluationCount = 2;
    dispatchProgress(p2);
    ASSERT_EQ(trainingState.plotBestSeries_.size(), 4u);
    EXPECT_FLOAT_EQ(trainingState.plotBestSeries_.back(), 0.8f);

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

TEST(StateTrainingTest, ServerDisconnectedWhileAlreadyDisconnectedStaysDisconnected)
{
    LvglTestDisplay lvgl;
    TestStateMachineFixture fixture;

    fixture.stateMachine->uiManager_ = std::make_unique<UiComponentManager>(lvgl.display);
    fixture.stateMachine->uiManager_->setEventSink(fixture.stateMachine.get());

    fixture.stateMachine->handleEvent(ServerDisconnectedEvent{ "Connection lost" });
    ASSERT_EQ(fixture.stateMachine->getCurrentStateName(), "Disconnected");

    fixture.stateMachine->handleEvent(ServerDisconnectedEvent{ "Connect failed" });
    EXPECT_EQ(fixture.stateMachine->getCurrentStateName(), "Disconnected");
}

TEST(StateTrainingTest, ConnectWaitsForServerConnectedEventBeforeTrainingActiveTransition)
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
    State::Any pendingState = disconnectedState.onEvent(
        ConnectToServerCommand{ .host = "localhost", .port = 8080 }, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<Disconnected>(pendingState.getVariant()));
    EXPECT_TRUE(fixture.mockWebSocketService->sentCommands().empty());

    auto& pendingDisconnected = std::get<Disconnected>(pendingState.getVariant());
    State::Any newState =
        pendingDisconnected.onEvent(ServerConnectedEvent{}, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<TrainingActive>(newState.getVariant()));

    const auto& sentCommands = fixture.mockWebSocketService->sentCommands();
    ASSERT_EQ(sentCommands.size(), 1u);
    EXPECT_EQ(sentCommands[0], "StatusGet");
}

TEST(StateTrainingTest, ConnectWaitsForServerConnectedEventBeforeStartMenuTransition)
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
    State::Any pendingState = disconnectedState.onEvent(
        ConnectToServerCommand{ .host = "localhost", .port = 8080 }, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<Disconnected>(pendingState.getVariant()));
    EXPECT_TRUE(fixture.mockWebSocketService->sentCommands().empty());

    auto& pendingDisconnected = std::get<Disconnected>(pendingState.getVariant());
    State::Any newState =
        pendingDisconnected.onEvent(ServerConnectedEvent{}, *fixture.stateMachine);

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

TEST(StateTrainingTest, StartEvolutionAllowsZeroGenerations)
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
    evt.evolution.maxGenerations = 0;
    evt.mutation.rate = 0.1;
    evt.training.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    evt.training.organismType = OrganismType::NES_DUCK;

    State::Any result = trainingState.onEvent(evt, *fixture.stateMachine);

    auto* activeState = std::get_if<TrainingActive>(&result.getVariant());
    ASSERT_NE(activeState, nullptr);

    const auto& sentCommands = fixture.mockWebSocketService->sentCommands();
    ASSERT_GE(sentCommands.size(), 4u);
    EXPECT_EQ(sentCommands[0], "TrainingStreamConfigSet");
    EXPECT_EQ(sentCommands[1], "RenderFormatSet");
    EXPECT_EQ(sentCommands[2], "EvolutionStart");
    EXPECT_EQ(sentCommands[3], "UserSettingsSet");

    trainingState.view_.reset();
    fixture.stateMachine->uiManager_.reset();
}

TEST(StateTrainingTest, TrainingIdleConfigUpdatePatchesServerUserSettings)
{
    TestStateMachineFixture fixture;

    Api::UserSettingsPatch::Okay settingsOkay{
        .settings = fixture.stateMachine->getServerUserSettings(),
    };
    settingsOkay.settings.evolutionConfig.maxSimulationTime = 40.0;
    settingsOkay.settings.evolutionConfig.populationSize = 37;
    settingsOkay.settings.mutationConfig.rate = 0.123;
    settingsOkay.settings.trainingSpec.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    settingsOkay.settings.trainingSpec.organismType = OrganismType::NES_DUCK;

    fixture.mockWebSocketService->expectSuccess<Api::UserSettingsPatch::Command>(settingsOkay);

    TrainingIdle trainingState;
    TrainingConfigUpdatedEvent evt{
        .evolution = settingsOkay.settings.evolutionConfig,
        .mutation = settingsOkay.settings.mutationConfig,
        .training = settingsOkay.settings.trainingSpec,
    };

    State::Any newState = trainingState.onEvent(evt, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<TrainingIdle>(newState.getVariant()));
    ASSERT_EQ(fixture.mockWebSocketService->sentCommands().size(), 1u);
    EXPECT_EQ(fixture.mockWebSocketService->sentCommands()[0], "UserSettingsPatch");

    const auto& local = fixture.stateMachine->getUserSettings();
    EXPECT_EQ(
        local.evolutionConfig.populationSize, settingsOkay.settings.evolutionConfig.populationSize);
    EXPECT_DOUBLE_EQ(
        local.evolutionConfig.maxSimulationTime,
        settingsOkay.settings.evolutionConfig.maxSimulationTime);
    EXPECT_DOUBLE_EQ(local.mutationConfig.rate, settingsOkay.settings.mutationConfig.rate);
    EXPECT_EQ(local.trainingSpec.scenarioId, settingsOkay.settings.trainingSpec.scenarioId);
    EXPECT_EQ(local.trainingSpec.organismType, settingsOkay.settings.trainingSpec.organismType);

    const auto& server = fixture.stateMachine->getServerUserSettings();
    EXPECT_EQ(
        server.evolutionConfig.populationSize,
        settingsOkay.settings.evolutionConfig.populationSize);
    EXPECT_DOUBLE_EQ(
        server.evolutionConfig.maxSimulationTime,
        settingsOkay.settings.evolutionConfig.maxSimulationTime);
    EXPECT_DOUBLE_EQ(server.mutationConfig.rate, settingsOkay.settings.mutationConfig.rate);
    EXPECT_EQ(server.trainingSpec.scenarioId, settingsOkay.settings.trainingSpec.scenarioId);
    EXPECT_EQ(server.trainingSpec.organismType, settingsOkay.settings.trainingSpec.organismType);
}

TEST(StateTrainingTest, TrainingActiveConfigUpdatePatchesServerUserSettings)
{
    TestStateMachineFixture fixture;

    Api::UserSettingsPatch::Okay settingsOkay{
        .settings = fixture.stateMachine->getServerUserSettings(),
    };
    settingsOkay.settings.evolutionConfig.maxSimulationTime = 55.0;
    settingsOkay.settings.evolutionConfig.populationSize = 19;
    settingsOkay.settings.mutationConfig.sigma = 0.222;
    settingsOkay.settings.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    settingsOkay.settings.trainingSpec.organismType = OrganismType::TREE;

    fixture.mockWebSocketService->expectSuccess<Api::UserSettingsPatch::Command>(settingsOkay);

    TrainingActive trainingState;
    TrainingConfigUpdatedEvent evt{
        .evolution = settingsOkay.settings.evolutionConfig,
        .mutation = settingsOkay.settings.mutationConfig,
        .training = settingsOkay.settings.trainingSpec,
    };

    State::Any newState = trainingState.onEvent(evt, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<TrainingActive>(newState.getVariant()));
    ASSERT_EQ(fixture.mockWebSocketService->sentCommands().size(), 1u);
    EXPECT_EQ(fixture.mockWebSocketService->sentCommands()[0], "UserSettingsPatch");

    const auto& local = fixture.stateMachine->getUserSettings();
    EXPECT_EQ(
        local.evolutionConfig.populationSize, settingsOkay.settings.evolutionConfig.populationSize);
    EXPECT_DOUBLE_EQ(
        local.evolutionConfig.maxSimulationTime,
        settingsOkay.settings.evolutionConfig.maxSimulationTime);
    EXPECT_DOUBLE_EQ(local.mutationConfig.sigma, settingsOkay.settings.mutationConfig.sigma);
    EXPECT_EQ(local.trainingSpec.scenarioId, settingsOkay.settings.trainingSpec.scenarioId);
    EXPECT_EQ(local.trainingSpec.organismType, settingsOkay.settings.trainingSpec.organismType);

    const auto& server = fixture.stateMachine->getServerUserSettings();
    EXPECT_EQ(
        server.evolutionConfig.populationSize,
        settingsOkay.settings.evolutionConfig.populationSize);
    EXPECT_DOUBLE_EQ(
        server.evolutionConfig.maxSimulationTime,
        settingsOkay.settings.evolutionConfig.maxSimulationTime);
    EXPECT_DOUBLE_EQ(server.mutationConfig.sigma, settingsOkay.settings.mutationConfig.sigma);
    EXPECT_EQ(server.trainingSpec.scenarioId, settingsOkay.settings.trainingSpec.scenarioId);
    EXPECT_EQ(server.trainingSpec.organismType, settingsOkay.settings.trainingSpec.organismType);
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
