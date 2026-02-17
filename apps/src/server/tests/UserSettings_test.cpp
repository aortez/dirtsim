#include "core/UUID.h"
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
    EXPECT_EQ(inMemory.timezoneIndex, 2);
    EXPECT_EQ(inMemory.volumePercent, 20);
    EXPECT_EQ(inMemory.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_FALSE(inMemory.startMenuAutoRun);
    EXPECT_EQ(inMemory.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);

    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.timezoneIndex, 2);
    EXPECT_EQ(fromDisk.volumePercent, 20);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_FALSE(fromDisk.startMenuAutoRun);
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
    population.scenarioId = Scenario::EnumType::TreeGermination;
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

TEST(UserSettingsTest, UserSettingsSetClampsAndPersists)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-set");

    bool callbackInvoked = false;
    Api::UserSettingsSet::Response response;

    Api::UserSettingsSet::Command command{
        .settings =
            UserSettings{
                .timezoneIndex = -50,
                .volumePercent = 999,
                .defaultScenario = Scenario::EnumType::Clock,
                .startMenuIdleAction = StartMenuIdleAction::ClockScenario,
                .startMenuAutoRun = true,
                .trainingSpec = {},
                .evolutionConfig = {},
                .mutationConfig = {},
                .trainingResumePolicy = static_cast<TrainingResumePolicy>(99),
            },
    };
    Api::UserSettingsSet::Cwc cwc(command, [&](Api::UserSettingsSet::Response&& result) {
        callbackInvoked = true;
        response = std::move(result);
    });

    fixture.stateMachine->handleEvent(Event{ cwc });

    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(response.isValue());
    EXPECT_EQ(response.value().settings.timezoneIndex, 0);
    EXPECT_EQ(response.value().settings.volumePercent, 100);
    EXPECT_EQ(response.value().settings.defaultScenario, Scenario::EnumType::Clock);
    EXPECT_TRUE(response.value().settings.startMenuAutoRun);
    EXPECT_EQ(response.value().settings.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.timezoneIndex, 0);
    EXPECT_EQ(fromDisk.volumePercent, 100);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Clock);
    EXPECT_TRUE(fromDisk.startMenuAutoRun);
    EXPECT_EQ(fromDisk.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
}

TEST(UserSettingsTest, UserSettingsResetRestoresDefaultsAndPersists)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-reset");

    Api::UserSettingsSet::Command setCommand{
        .settings =
            UserSettings{
                .timezoneIndex = 7,
                .volumePercent = 65,
                .defaultScenario = Scenario::EnumType::Clock,
                .startMenuIdleAction = StartMenuIdleAction::ClockScenario,
                .startMenuAutoRun = true,
                .trainingSpec = {},
                .evolutionConfig = {},
                .mutationConfig = {},
            },
    };
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
    EXPECT_EQ(response.value().settings.timezoneIndex, 2);
    EXPECT_EQ(response.value().settings.volumePercent, 20);
    EXPECT_EQ(response.value().settings.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_FALSE(response.value().settings.startMenuAutoRun);
    EXPECT_EQ(response.value().settings.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.timezoneIndex, 2);
    EXPECT_EQ(fromDisk.volumePercent, 20);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Sandbox);
    EXPECT_FALSE(fromDisk.startMenuAutoRun);
    EXPECT_EQ(fromDisk.trainingResumePolicy, TrainingResumePolicy::WarmFromBest);
}

TEST(UserSettingsTest, UserSettingsPatchMergesAndPersists)
{
    TestStateMachineFixture fixture("dirtsim-user-settings-patch");

    UserSettings baseSettings = fixture.stateMachine->getUserSettings();
    baseSettings.timezoneIndex = 7;
    baseSettings.volumePercent = 65;
    baseSettings.defaultScenario = Scenario::EnumType::Clock;
    baseSettings.startMenuIdleAction = StartMenuIdleAction::TrainingSession;
    baseSettings.startMenuAutoRun = true;
    baseSettings.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    baseSettings.trainingSpec.organismType = OrganismType::TREE;
    baseSettings.trainingSpec.population.clear();

    Api::UserSettingsSet::Command setCommand{ .settings = baseSettings };
    Api::UserSettingsSet::Cwc setCwc(setCommand, [](Api::UserSettingsSet::Response&&) {});
    fixture.stateMachine->handleEvent(Event{ setCwc });

    bool callbackInvoked = false;
    Api::UserSettingsPatch::Response response;

    TrainingSpec updatedTrainingSpec;
    updatedTrainingSpec.scenarioId = Scenario::EnumType::DuckTraining;
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
    EXPECT_EQ(inMemory.timezoneIndex, 7);
    EXPECT_EQ(inMemory.volumePercent, 65);
    EXPECT_EQ(inMemory.defaultScenario, Scenario::EnumType::Clock);
    EXPECT_EQ(inMemory.startMenuIdleAction, StartMenuIdleAction::TrainingSession);
    EXPECT_TRUE(inMemory.startMenuAutoRun);
    EXPECT_EQ(inMemory.trainingSpec.scenarioId, Scenario::EnumType::Clock);
    EXPECT_EQ(inMemory.trainingSpec.organismType, OrganismType::DUCK);

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.timezoneIndex, 7);
    EXPECT_EQ(fromDisk.volumePercent, 65);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Clock);
    EXPECT_EQ(fromDisk.startMenuIdleAction, StartMenuIdleAction::TrainingSession);
    EXPECT_TRUE(fromDisk.startMenuAutoRun);
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
