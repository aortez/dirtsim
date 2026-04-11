#include "core/UUID.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/scenarios/ClockTimezone.h"
#include "server/Event.h"
#include "server/UserSettings.h"
#include "server/api/NesFrameDelaySet.h"
#include "server/api/TrainingResult.h"
#include "server/api/UserSettingsPatch.h"
#include "server/api/UserSettingsReset.h"
#include "server/api/UserSettingsSet.h"
#include "server/tests/TestStateMachineFixture.h"
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::Tests;

namespace {

constexpr double kMaxPersistedNesFrameDelayMs = 1000.0 / 60.0988 - 0.001;

UserSettings readUserSettingsFromDisk(const std::filesystem::path& path)
{
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "Failed to open user settings file: " << path;

    nlohmann::json json;
    file >> json;
    return json.get<UserSettings>();
}

nlohmann::json readUserSettingsJsonFromDisk(const std::filesystem::path& path)
{
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "Failed to open user settings file: " << path;

    nlohmann::json json;
    file >> json;
    return json;
}

} // namespace

TEST(UserSettingsTest, MissingFileLoadsDefaultsAndWritesFile)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-defaults");

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    ASSERT_TRUE(std::filesystem::exists(settingsPath));

    const UserSettings& inMemory = fixture.stateMachine->getUserSettings();
    EXPECT_EQ(inMemory.clockScenarioConfig.timezone, Config::ClockTimezone::LosAngeles);
    EXPECT_FALSE(inMemory.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(inMemory.nesSessionSettings.frameDelayMs, 0.0);
    EXPECT_EQ(inMemory.searchSettings.maxSearchedNodeCount, 5000u);
    EXPECT_EQ(inMemory.searchSettings.stallFrameLimit, 30u);
    EXPECT_EQ(inMemory.volumePercent, 20);
    EXPECT_EQ(inMemory.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_EQ(inMemory.startMenuIdleTimeoutMs, 60000);
    EXPECT_EQ(inMemory.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
    EXPECT_FALSE(inMemory.networkLiveScanPreferred);

    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.clockScenarioConfig.timezone, Config::ClockTimezone::LosAngeles);
    EXPECT_FALSE(fromDisk.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(fromDisk.nesSessionSettings.frameDelayMs, 0.0);
    EXPECT_EQ(fromDisk.searchSettings.maxSearchedNodeCount, 5000u);
    EXPECT_EQ(fromDisk.searchSettings.stallFrameLimit, 30u);
    EXPECT_EQ(fromDisk.volumePercent, 20);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_EQ(fromDisk.startMenuIdleTimeoutMs, 60000);
    EXPECT_EQ(fromDisk.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
    EXPECT_FALSE(fromDisk.networkLiveScanPreferred);
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

TEST(UserSettingsTest, LoadingLegacySettingsBackfillsDefaultsAndStripsUnknownFields)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-compat");
    fixture.stateMachine.reset();

    UserSettings legacySettings{};
    legacySettings.clockScenarioConfig.timezone = Config::ClockTimezone::Paris;
    legacySettings.nesSessionSettings.frameDelayEnabled = true;
    legacySettings.volumePercent = 65;
    legacySettings.defaultScenario = Scenario::EnumType::Clock;
    legacySettings.uiTraining.streamIntervalMs = 24;

    nlohmann::json legacyJson = legacySettings;
    legacyJson.erase("trainingResumePolicy");
    legacyJson.erase("treeGerminationScenarioConfig");
    legacyJson.erase("searchSettings");
    legacyJson["nesSessionSettings"].erase("frameDelayMs");
    legacyJson["uiTraining"].erase("bestPlaybackIntervalMs");
    legacyJson["futureSetting"] = 123;
    legacyJson["clockScenarioConfig"]["futureClockToggle"] = true;
    legacyJson["uiTraining"]["futureOverlayMode"] = "on";

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    std::ofstream file(settingsPath);
    ASSERT_TRUE(file.is_open());
    file << legacyJson.dump(2) << "\n";
    file.close();

    auto mockWs = std::make_unique<MockWebSocketService>();
    fixture.mockWebSocketService = mockWs.get();
    fixture.mockWebSocketService->expectSuccess<Api::TrainingResult>(std::monostate{});
    fixture.stateMachine = std::make_unique<StateMachine>(std::move(mockWs), fixture.testDataDir);

    const UserSettings& inMemory = fixture.stateMachine->getUserSettings();
    EXPECT_EQ(inMemory.clockScenarioConfig.timezone, Config::ClockTimezone::Paris);
    EXPECT_TRUE(inMemory.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(inMemory.nesSessionSettings.frameDelayMs, 0.0);
    EXPECT_EQ(inMemory.searchSettings.maxSearchedNodeCount, 5000u);
    EXPECT_EQ(inMemory.searchSettings.stallFrameLimit, 30u);
    EXPECT_TRUE(inMemory.searchSettings.belowScreenPruningEnabled);
    EXPECT_TRUE(inMemory.searchSettings.velocityPruningEnabled);
    EXPECT_EQ(
        inMemory.treeGerminationScenarioConfig.brain_type,
        UserSettings{}.treeGerminationScenarioConfig.brain_type);
    EXPECT_EQ(inMemory.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
    EXPECT_EQ(inMemory.uiTraining.streamIntervalMs, 24);
    EXPECT_EQ(inMemory.uiTraining.bestPlaybackIntervalMs, 16);

    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.clockScenarioConfig.timezone, Config::ClockTimezone::Paris);
    EXPECT_TRUE(fromDisk.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(fromDisk.nesSessionSettings.frameDelayMs, 0.0);
    EXPECT_EQ(fromDisk.searchSettings.maxSearchedNodeCount, 5000u);
    EXPECT_EQ(fromDisk.searchSettings.stallFrameLimit, 30u);
    EXPECT_TRUE(fromDisk.searchSettings.belowScreenPruningEnabled);
    EXPECT_TRUE(fromDisk.searchSettings.velocityPruningEnabled);
    EXPECT_EQ(
        fromDisk.treeGerminationScenarioConfig.brain_type,
        UserSettings{}.treeGerminationScenarioConfig.brain_type);
    EXPECT_EQ(fromDisk.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
    EXPECT_EQ(fromDisk.uiTraining.streamIntervalMs, 24);
    EXPECT_EQ(fromDisk.uiTraining.bestPlaybackIntervalMs, 16);

    const nlohmann::json canonicalJson = readUserSettingsJsonFromDisk(settingsPath);
    EXPECT_FALSE(canonicalJson.contains("futureSetting"));
    ASSERT_TRUE(canonicalJson.contains("clockScenarioConfig"));
    EXPECT_FALSE(canonicalJson["clockScenarioConfig"].contains("futureClockToggle"));
    ASSERT_TRUE(canonicalJson.contains("uiTraining"));
    EXPECT_FALSE(canonicalJson["uiTraining"].contains("futureOverlayMode"));
    ASSERT_TRUE(canonicalJson.contains("nesSessionSettings"));
    EXPECT_TRUE(canonicalJson.contains("searchSettings"));
    EXPECT_TRUE(canonicalJson["searchSettings"].contains("belowScreenPruningEnabled"));
    EXPECT_TRUE(canonicalJson.contains("trainingResumePolicy"));
    EXPECT_TRUE(canonicalJson.contains("treeGerminationScenarioConfig"));
    EXPECT_TRUE(canonicalJson["nesSessionSettings"].contains("frameDelayMs"));
    EXPECT_TRUE(canonicalJson["uiTraining"].contains("bestPlaybackIntervalMs"));
}

TEST(UserSettingsTest, UserSettingsSetJsonRemainsStrictForMissingFields)
{
    const nlohmann::json json = {
        { "settings",
          {
              { "clockScenarioConfig", nlohmann::json(UserSettings{}.clockScenarioConfig) },
              { "volumePercent", 55 },
              { "defaultScenario", "Clock" },
          } },
    };

    EXPECT_THROW((void)Api::UserSettingsSet::Command::fromJson(json), std::runtime_error);
}

TEST(UserSettingsTest, UserSettingsSetClampsAndPersists)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-set");
    bool callbackInvoked = false;
    Api::UserSettingsSet::Response response;

    UserSettings requestedSettings{};
    requestedSettings.clockScenarioConfig.timezone = static_cast<Config::ClockTimezone>(255);
    requestedSettings.nesSessionSettings.frameDelayEnabled = true;
    requestedSettings.nesSessionSettings.frameDelayMs = 999.0;
    requestedSettings.searchSettings = SearchSettings{
        .maxSearchedNodeCount = 0u,
        .stallFrameLimit = 999u,
        .velocityPruningEnabled = false,
        .belowScreenPruningEnabled = false,
    };
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
    EXPECT_EQ(
        response.value().settings.clockScenarioConfig.timezone, Config::ClockTimezone::LosAngeles);
    EXPECT_TRUE(response.value().settings.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(
        response.value().settings.nesSessionSettings.frameDelayMs, kMaxPersistedNesFrameDelayMs);
    EXPECT_EQ(
        response.value().settings.searchSettings.maxSearchedNodeCount,
        SearchSettings::MaxSearchedNodeCountMin);
    EXPECT_EQ(
        response.value().settings.searchSettings.stallFrameLimit,
        SearchSettings::StallFrameLimitMax);
    EXPECT_FALSE(response.value().settings.searchSettings.belowScreenPruningEnabled);
    EXPECT_FALSE(response.value().settings.searchSettings.velocityPruningEnabled);
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
    EXPECT_EQ(fromDisk.clockScenarioConfig.timezone, Config::ClockTimezone::LosAngeles);
    EXPECT_TRUE(fromDisk.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(fromDisk.nesSessionSettings.frameDelayMs, kMaxPersistedNesFrameDelayMs);
    EXPECT_EQ(
        fromDisk.searchSettings.maxSearchedNodeCount, SearchSettings::MaxSearchedNodeCountMin);
    EXPECT_EQ(fromDisk.searchSettings.stallFrameLimit, SearchSettings::StallFrameLimitMax);
    EXPECT_FALSE(fromDisk.searchSettings.belowScreenPruningEnabled);
    EXPECT_FALSE(fromDisk.searchSettings.velocityPruningEnabled);
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
    changedSettings.clockScenarioConfig.timezone = Config::ClockTimezone::Paris;
    changedSettings.nesSessionSettings.frameDelayEnabled = true;
    changedSettings.nesSessionSettings.frameDelayMs = 4.2;
    changedSettings.searchSettings = SearchSettings{
        .maxSearchedNodeCount = 10000u,
        .stallFrameLimit = 60u,
        .velocityPruningEnabled = false,
        .belowScreenPruningEnabled = false,
    };
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
    EXPECT_EQ(
        response.value().settings.clockScenarioConfig.timezone, Config::ClockTimezone::LosAngeles);
    EXPECT_FALSE(response.value().settings.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(response.value().settings.nesSessionSettings.frameDelayMs, 0.0);
    EXPECT_EQ(response.value().settings.searchSettings.maxSearchedNodeCount, 5000u);
    EXPECT_EQ(response.value().settings.searchSettings.stallFrameLimit, 30u);
    EXPECT_TRUE(response.value().settings.searchSettings.belowScreenPruningEnabled);
    EXPECT_TRUE(response.value().settings.searchSettings.velocityPruningEnabled);
    EXPECT_EQ(response.value().settings.volumePercent, 20);
    EXPECT_EQ(response.value().settings.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_EQ(response.value().settings.startMenuIdleTimeoutMs, 60000);
    EXPECT_EQ(response.value().settings.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.clockScenarioConfig.timezone, Config::ClockTimezone::LosAngeles);
    EXPECT_FALSE(fromDisk.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(fromDisk.nesSessionSettings.frameDelayMs, 0.0);
    EXPECT_EQ(fromDisk.searchSettings.maxSearchedNodeCount, 5000u);
    EXPECT_EQ(fromDisk.searchSettings.stallFrameLimit, 30u);
    EXPECT_TRUE(fromDisk.searchSettings.belowScreenPruningEnabled);
    EXPECT_TRUE(fromDisk.searchSettings.velocityPruningEnabled);
    EXPECT_EQ(fromDisk.volumePercent, 20);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_EQ(fromDisk.startMenuIdleTimeoutMs, 60000);
    EXPECT_EQ(fromDisk.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
}

TEST(UserSettingsTest, UserSettingsPatchMergesAndPersists)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-patch");

    UserSettings baseSettings = fixture.stateMachine->getUserSettings();
    baseSettings.clockScenarioConfig.timezone = Config::ClockTimezone::Paris;
    baseSettings.nesSessionSettings.frameDelayEnabled = true;
    baseSettings.nesSessionSettings.frameDelayMs = 4.2;
    baseSettings.searchSettings = SearchSettings{
        .maxSearchedNodeCount = 8000u,
        .stallFrameLimit = 50u,
        .velocityPruningEnabled = true,
        .belowScreenPruningEnabled = true,
    };
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
    patchCommand.searchSettings = SearchSettings{
        .maxSearchedNodeCount = 999999u,
        .stallFrameLimit = 999u,
        .velocityPruningEnabled = false,
        .belowScreenPruningEnabled = false,
    };
    patchCommand.networkLiveScanPreferred = true;
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
    EXPECT_EQ(inMemory.clockScenarioConfig.timezone, Config::ClockTimezone::Paris);
    EXPECT_TRUE(inMemory.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(inMemory.nesSessionSettings.frameDelayMs, 4.2);
    EXPECT_EQ(
        inMemory.searchSettings.maxSearchedNodeCount, SearchSettings::MaxSearchedNodeCountMax);
    EXPECT_EQ(inMemory.searchSettings.stallFrameLimit, SearchSettings::StallFrameLimitMax);
    EXPECT_FALSE(inMemory.searchSettings.belowScreenPruningEnabled);
    EXPECT_FALSE(inMemory.searchSettings.velocityPruningEnabled);
    EXPECT_EQ(inMemory.volumePercent, 65);
    EXPECT_EQ(inMemory.defaultScenario, Scenario::EnumType::Clock);
    EXPECT_EQ(inMemory.startMenuIdleAction, StartMenuIdleAction::TrainingSession);
    EXPECT_EQ(inMemory.startMenuIdleTimeoutMs, 90000);
    EXPECT_EQ(inMemory.trainingSpec.scenarioId, Scenario::EnumType::Clock);
    EXPECT_EQ(inMemory.trainingSpec.organismType, OrganismType::DUCK);
    EXPECT_TRUE(inMemory.networkLiveScanPreferred);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.clockScenarioConfig.timezone, Config::ClockTimezone::Paris);
    EXPECT_TRUE(fromDisk.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(fromDisk.nesSessionSettings.frameDelayMs, 4.2);
    EXPECT_EQ(
        fromDisk.searchSettings.maxSearchedNodeCount, SearchSettings::MaxSearchedNodeCountMax);
    EXPECT_EQ(fromDisk.searchSettings.stallFrameLimit, SearchSettings::StallFrameLimitMax);
    EXPECT_FALSE(fromDisk.searchSettings.belowScreenPruningEnabled);
    EXPECT_FALSE(fromDisk.searchSettings.velocityPruningEnabled);
    EXPECT_EQ(fromDisk.volumePercent, 65);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Clock);
    EXPECT_EQ(fromDisk.startMenuIdleAction, StartMenuIdleAction::TrainingSession);
    EXPECT_EQ(fromDisk.startMenuIdleTimeoutMs, 90000);
    EXPECT_EQ(fromDisk.trainingSpec.scenarioId, Scenario::EnumType::Clock);
    EXPECT_EQ(fromDisk.trainingSpec.organismType, OrganismType::DUCK);
    EXPECT_TRUE(fromDisk.networkLiveScanPreferred);
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

TEST(UserSettingsTest, NesFrameDelaySetPersistsIntoUserSettingsAndLiveState)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-nes-frame-delay-set");

    bool callbackInvoked = false;
    Api::NesFrameDelaySet::Response response;
    Api::NesFrameDelaySet::Command command{ .enabled = true, .frame_delay_ms = 3.5 };
    Api::NesFrameDelaySet::Cwc cwc(command, [&](Api::NesFrameDelaySet::Response&& result) {
        callbackInvoked = true;
        response = std::move(result);
    });

    fixture.stateMachine->handleEvent(Event{ cwc });

    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(response.isValue());
    EXPECT_TRUE(response.value().enabled);
    EXPECT_DOUBLE_EQ(response.value().frame_delay_ms, 3.5);
    EXPECT_TRUE(fixture.stateMachine->isNesFrameDelayEnabled());
    EXPECT_DOUBLE_EQ(fixture.stateMachine->getNesFrameDelayMs(), 3.5);
    EXPECT_TRUE(fixture.stateMachine->getUserSettings().nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(fixture.stateMachine->getUserSettings().nesSessionSettings.frameDelayMs, 3.5);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_TRUE(fromDisk.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(fromDisk.nesSessionSettings.frameDelayMs, 3.5);
}

TEST(UserSettingsTest, NesFrameDelaySetClampsAndPersists)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-nes-frame-delay-set-clamp");

    bool callbackInvoked = false;
    Api::NesFrameDelaySet::Response response;
    Api::NesFrameDelaySet::Command command{ .enabled = true, .frame_delay_ms = 999.0 };
    Api::NesFrameDelaySet::Cwc cwc(command, [&](Api::NesFrameDelaySet::Response&& result) {
        callbackInvoked = true;
        response = std::move(result);
    });

    fixture.stateMachine->handleEvent(Event{ cwc });

    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(response.isValue());
    EXPECT_TRUE(response.value().enabled);
    EXPECT_DOUBLE_EQ(response.value().frame_delay_ms, kMaxPersistedNesFrameDelayMs);
    EXPECT_TRUE(fixture.stateMachine->isNesFrameDelayEnabled());
    EXPECT_DOUBLE_EQ(fixture.stateMachine->getNesFrameDelayMs(), kMaxPersistedNesFrameDelayMs);
    EXPECT_TRUE(fixture.stateMachine->getUserSettings().nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(
        fixture.stateMachine->getUserSettings().nesSessionSettings.frameDelayMs,
        kMaxPersistedNesFrameDelayMs);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_TRUE(fromDisk.nesSessionSettings.frameDelayEnabled);
    EXPECT_DOUBLE_EQ(fromDisk.nesSessionSettings.frameDelayMs, kMaxPersistedNesFrameDelayMs);
}
