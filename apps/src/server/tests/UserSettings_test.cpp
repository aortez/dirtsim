#include "core/UUID.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/scenarios/ClockScenario.h"
#include "server/Event.h"
#include "server/UserSettings.h"
#include "server/api/TrainingResult.h"
#include "server/api/UserSettingsPatch.h"
#include "server/api/UserSettingsReset.h"
#include "server/api/UserSettingsSet.h"
#include "server/tests/TestStateMachineFixture.h"
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::Tests;

namespace {

UserSettings readUserSettingsFromDisk(const std::filesystem::path& path)
{
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "Failed to open user settings file: " << path;

    nlohmann::json json;
    file >> json;
    return json.get<UserSettings>();
}

} // namespace

TEST(UserSettingsTest, MissingFileLoadsDefaultsAndWritesFile)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-defaults");

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    ASSERT_TRUE(std::filesystem::exists(settingsPath));

    const UserSettings& inMemory = fixture.stateMachine->getUserSettings();
    EXPECT_EQ(inMemory.clockScenarioConfig.timezoneIndex, 2);
    EXPECT_EQ(inMemory.volumePercent, 20);
    EXPECT_EQ(inMemory.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_EQ(inMemory.startMenuIdleTimeoutMs, 60000);
    EXPECT_EQ(inMemory.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);

    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.clockScenarioConfig.timezoneIndex, 2);
    EXPECT_EQ(fromDisk.volumePercent, 20);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_EQ(fromDisk.startMenuIdleTimeoutMs, 60000);
    EXPECT_EQ(fromDisk.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
}

TEST(UserSettingsTest, LoadingSettingsScrubsMissingSeedGenomes)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-sanitize-seeds");
    fixture.stateMachine.reset();

    UserSettings staleSettings;
    staleSettings.startMenuIdleAction = StartMenuIdleAction::TrainingSession;
    staleSettings.trainingSpec.organismType = OrganismType::TREE;
    staleSettings.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;

    PopulationSpec population;
    population.brainKind = "NeuralNet";
    population.count = 2;
    population.randomCount = 1;
    population.seedGenomes.push_back(UUID::generate());
    staleSettings.trainingSpec.population.push_back(population);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    std::ofstream file(settingsPath);
    ASSERT_TRUE(file.is_open());
    nlohmann::json json = staleSettings;
    file << json.dump(2) << "\n";
    file.close();

    auto mockWs = std::make_unique<MockWebSocketService>();
    fixture.mockWebSocketService = mockWs.get();
    fixture.mockWebSocketService->expectSuccess<Api::TrainingResult>(std::monostate{});
    fixture.stateMachine = std::make_unique<StateMachine>(std::move(mockWs), fixture.testDataDir);

    const UserSettings& inMemory = fixture.stateMachine->getUserSettings();
    ASSERT_EQ(inMemory.trainingSpec.population.size(), 1u);
    const PopulationSpec& inMemoryPopulation = inMemory.trainingSpec.population.front();
    EXPECT_EQ(inMemoryPopulation.count, 2);
    EXPECT_EQ(inMemoryPopulation.randomCount, 2);
    EXPECT_TRUE(inMemoryPopulation.seedGenomes.empty());

    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    ASSERT_EQ(fromDisk.trainingSpec.population.size(), 1u);
    const PopulationSpec& diskPopulation = fromDisk.trainingSpec.population.front();
    EXPECT_EQ(diskPopulation.count, 2);
    EXPECT_EQ(diskPopulation.randomCount, 2);
    EXPECT_TRUE(diskPopulation.seedGenomes.empty());
}

TEST(UserSettingsTest, LoadingSettingsPromotesNesDuckTargetToNesOrganismWithoutBrainRewrite)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-sanitize-nes-target");
    fixture.stateMachine.reset();

    UserSettings staleSettings;
    staleSettings.startMenuIdleAction = StartMenuIdleAction::TrainingSession;
    staleSettings.trainingSpec.organismType = OrganismType::DUCK;
    staleSettings.trainingSpec.scenarioId = Scenario::EnumType::NesFlappyParatroopa;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::DuckNeuralNetRecurrent;
    population.count = 2;
    population.randomCount = 2;
    staleSettings.trainingSpec.population.push_back(population);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    std::ofstream file(settingsPath);
    ASSERT_TRUE(file.is_open());
    nlohmann::json json = staleSettings;
    file << json.dump(2) << "\n";
    file.close();

    auto mockWs = std::make_unique<MockWebSocketService>();
    fixture.mockWebSocketService = mockWs.get();
    fixture.mockWebSocketService->expectSuccess<Api::TrainingResult>(std::monostate{});
    fixture.stateMachine = std::make_unique<StateMachine>(std::move(mockWs), fixture.testDataDir);

    const UserSettings& inMemory = fixture.stateMachine->getUserSettings();
    EXPECT_EQ(inMemory.trainingSpec.organismType, OrganismType::NES_FLAPPY_BIRD);
    EXPECT_EQ(inMemory.trainingSpec.scenarioId, Scenario::EnumType::NesFlappyParatroopa);
    ASSERT_EQ(inMemory.trainingSpec.population.size(), 1u);
    const PopulationSpec& inMemoryPopulation = inMemory.trainingSpec.population.front();
    EXPECT_EQ(inMemoryPopulation.brainKind, TrainingBrainKind::DuckNeuralNetRecurrent);
    EXPECT_EQ(inMemoryPopulation.count, 2);
    EXPECT_EQ(inMemoryPopulation.randomCount, 2);

    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.trainingSpec.organismType, OrganismType::NES_FLAPPY_BIRD);
    EXPECT_EQ(fromDisk.trainingSpec.scenarioId, Scenario::EnumType::NesFlappyParatroopa);
    ASSERT_EQ(fromDisk.trainingSpec.population.size(), 1u);
    const PopulationSpec& diskPopulation = fromDisk.trainingSpec.population.front();
    EXPECT_EQ(diskPopulation.brainKind, TrainingBrainKind::DuckNeuralNetRecurrent);
    EXPECT_EQ(diskPopulation.count, 2);
    EXPECT_EQ(diskPopulation.randomCount, 2);
}

TEST(UserSettingsTest, UserSettingsSetClampsAndPersists)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-set");
    const uint8_t maxTimezoneIndex = static_cast<uint8_t>(ClockScenario::TIMEZONES.size() - 1);

    bool callbackInvoked = false;
    Api::UserSettingsSet::Response response;

    UserSettings requestedSettings{};
    requestedSettings.clockScenarioConfig.timezoneIndex = 255;
    requestedSettings.volumePercent = 999;
    requestedSettings.defaultScenario = Scenario::EnumType::Clock;
    requestedSettings.startMenuIdleAction = StartMenuIdleAction::ClockScenario;
    requestedSettings.startMenuIdleTimeoutMs = 99999999;
    requestedSettings.evolutionConfig = EvolutionConfig{
        .genomeArchiveMaxSize = 50000,
        .diversityEliteCount = -5,
        .diversityEliteFitnessEpsilon = -0.5,
        .warmStartSeedPercent = 999.0,
        .warmStartNoveltyWeight = -0.5,
        .warmStartFitnessFloorPercentile = 999.0,
    };
    requestedSettings.trainingResumePolicy = static_cast<TrainingResumePolicy>(99);

    Api::UserSettingsSet::Command command{ .settings = requestedSettings };
    Api::UserSettingsSet::Cwc cwc(command, [&](Api::UserSettingsSet::Response&& result) {
        callbackInvoked = true;
        response = std::move(result);
    });

    fixture.stateMachine->handleEvent(Event{ cwc });

    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(response.isValue());
    EXPECT_EQ(response.value().settings.clockScenarioConfig.timezoneIndex, maxTimezoneIndex);
    EXPECT_EQ(response.value().settings.volumePercent, 100);
    EXPECT_EQ(response.value().settings.defaultScenario, Scenario::EnumType::Clock);
    EXPECT_EQ(response.value().settings.startMenuIdleTimeoutMs, 3600000);
    EXPECT_EQ(response.value().settings.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
    EXPECT_EQ(response.value().settings.evolutionConfig.genomeArchiveMaxSize, 1000);
    EXPECT_DOUBLE_EQ(response.value().settings.evolutionConfig.warmStartSeedPercent, 100.0);
    EXPECT_DOUBLE_EQ(
        response.value().settings.evolutionConfig.warmStartFitnessFloorPercentile, 100.0);
    EXPECT_DOUBLE_EQ(response.value().settings.evolutionConfig.warmStartNoveltyWeight, 0.0);
    EXPECT_EQ(response.value().settings.evolutionConfig.diversityEliteCount, 0);
    EXPECT_DOUBLE_EQ(response.value().settings.evolutionConfig.diversityEliteFitnessEpsilon, 0.0);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.clockScenarioConfig.timezoneIndex, maxTimezoneIndex);
    EXPECT_EQ(fromDisk.volumePercent, 100);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Clock);
    EXPECT_EQ(fromDisk.startMenuIdleTimeoutMs, 3600000);
    EXPECT_EQ(fromDisk.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
    EXPECT_EQ(fromDisk.evolutionConfig.genomeArchiveMaxSize, 1000);
    EXPECT_DOUBLE_EQ(fromDisk.evolutionConfig.warmStartSeedPercent, 100.0);
    EXPECT_DOUBLE_EQ(fromDisk.evolutionConfig.warmStartFitnessFloorPercentile, 100.0);
    EXPECT_DOUBLE_EQ(fromDisk.evolutionConfig.warmStartNoveltyWeight, 0.0);
    EXPECT_EQ(fromDisk.evolutionConfig.diversityEliteCount, 0);
    EXPECT_DOUBLE_EQ(fromDisk.evolutionConfig.diversityEliteFitnessEpsilon, 0.0);
}

TEST(UserSettingsTest, UserSettingsResetRestoresDefaultsAndPersists)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-reset");

    UserSettings changedSettings{};
    changedSettings.clockScenarioConfig.timezoneIndex = 7;
    changedSettings.volumePercent = 65;
    changedSettings.defaultScenario = Scenario::EnumType::Clock;
    changedSettings.startMenuIdleAction = StartMenuIdleAction::ClockScenario;
    changedSettings.startMenuIdleTimeoutMs = 90000;

    Api::UserSettingsSet::Command setCommand{ .settings = changedSettings };
    Api::UserSettingsSet::Cwc setCwc(setCommand, [](Api::UserSettingsSet::Response&&) {});
    fixture.stateMachine->handleEvent(Event{ setCwc });

    bool callbackInvoked = false;
    Api::UserSettingsReset::Response response;
    Api::UserSettingsReset::Command resetCommand{};
    Api::UserSettingsReset::Cwc resetCwc(
        resetCommand, [&](Api::UserSettingsReset::Response&& result) {
            callbackInvoked = true;
            response = std::move(result);
        });

    fixture.stateMachine->handleEvent(Event{ resetCwc });

    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(response.isValue());
    EXPECT_EQ(response.value().settings.clockScenarioConfig.timezoneIndex, 2);
    EXPECT_EQ(response.value().settings.volumePercent, 20);
    EXPECT_EQ(response.value().settings.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_EQ(response.value().settings.startMenuIdleTimeoutMs, 60000);
    EXPECT_EQ(response.value().settings.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.clockScenarioConfig.timezoneIndex, 2);
    EXPECT_EQ(fromDisk.volumePercent, 20);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_EQ(fromDisk.startMenuIdleTimeoutMs, 60000);
    EXPECT_EQ(fromDisk.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
}

TEST(UserSettingsTest, UserSettingsPatchMergesAndPersists)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-patch");

    UserSettings baseSettings = fixture.stateMachine->getUserSettings();
    baseSettings.clockScenarioConfig.timezoneIndex = 7;
    baseSettings.volumePercent = 65;
    baseSettings.defaultScenario = Scenario::EnumType::Clock;
    baseSettings.startMenuIdleAction = StartMenuIdleAction::TrainingSession;
    baseSettings.startMenuIdleTimeoutMs = 90000;
    baseSettings.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    baseSettings.trainingSpec.organismType = OrganismType::TREE;
    baseSettings.trainingSpec.population.clear();

    Api::UserSettingsSet::Command setCommand{ .settings = baseSettings };
    Api::UserSettingsSet::Cwc setCwc(setCommand, [](Api::UserSettingsSet::Response&&) {});
    fixture.stateMachine->handleEvent(Event{ setCwc });

    bool callbackInvoked = false;
    Api::UserSettingsPatch::Response response;

    TrainingSpec updatedTrainingSpec;
    updatedTrainingSpec.scenarioId = Scenario::EnumType::Clock;
    updatedTrainingSpec.organismType = OrganismType::DUCK;
    updatedTrainingSpec.population.clear();

    Api::UserSettingsPatch::Command patchCommand{};
    patchCommand.trainingSpec = updatedTrainingSpec;
    Api::UserSettingsPatch::Cwc patchCwc(
        patchCommand, [&](Api::UserSettingsPatch::Response&& result) {
            callbackInvoked = true;
            response = std::move(result);
        });

    fixture.stateMachine->handleEvent(Event{ patchCwc });

    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(response.isValue());

    const UserSettings& inMemory = response.value().settings;
    EXPECT_EQ(inMemory.clockScenarioConfig.timezoneIndex, 7);
    EXPECT_EQ(inMemory.volumePercent, 65);
    EXPECT_EQ(inMemory.defaultScenario, Scenario::EnumType::Clock);
    EXPECT_EQ(inMemory.startMenuIdleAction, StartMenuIdleAction::TrainingSession);
    EXPECT_EQ(inMemory.startMenuIdleTimeoutMs, 90000);
    EXPECT_EQ(inMemory.trainingSpec.scenarioId, Scenario::EnumType::Clock);
    EXPECT_EQ(inMemory.trainingSpec.organismType, OrganismType::DUCK);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.clockScenarioConfig.timezoneIndex, 7);
    EXPECT_EQ(fromDisk.volumePercent, 65);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Clock);
    EXPECT_EQ(fromDisk.startMenuIdleAction, StartMenuIdleAction::TrainingSession);
    EXPECT_EQ(fromDisk.startMenuIdleTimeoutMs, 90000);
    EXPECT_EQ(fromDisk.trainingSpec.scenarioId, Scenario::EnumType::Clock);
    EXPECT_EQ(fromDisk.trainingSpec.organismType, OrganismType::DUCK);
}

TEST(UserSettingsTest, UserSettingsPatchRejectsEmptyCommand)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-patch-empty");

    bool callbackInvoked = false;
    Api::UserSettingsPatch::Response response;
    Api::UserSettingsPatch::Command patchCommand{};
    Api::UserSettingsPatch::Cwc patchCwc(
        patchCommand, [&](Api::UserSettingsPatch::Response&& result) {
            callbackInvoked = true;
            response = std::move(result);
        });

    fixture.stateMachine->handleEvent(Event{ patchCwc });

    ASSERT_TRUE(callbackInvoked);
    EXPECT_TRUE(response.isError());
    if (response.isError()) {
        EXPECT_EQ(response.errorValue().message, "No fields provided to patch");
    }
}
