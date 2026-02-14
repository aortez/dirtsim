#include "server/Event.h"
#include "server/UserSettings.h"
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

    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.timezoneIndex, 2);
    EXPECT_EQ(fromDisk.volumePercent, 20);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Sandbox);
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
                .trainingSpec = {},
                .evolutionConfig = {},
                .mutationConfig = {},
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

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.timezoneIndex, 0);
    EXPECT_EQ(fromDisk.volumePercent, 100);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Clock);
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

    const std::filesystem::path settingsPath = fixture.testDataDir / "user_settings.json";
    const UserSettings fromDisk = readUserSettingsFromDisk(settingsPath);
    EXPECT_EQ(fromDisk.timezoneIndex, 2);
    EXPECT_EQ(fromDisk.volumePercent, 20);
    EXPECT_EQ(fromDisk.defaultScenario, Scenario::EnumType::Sandbox);
}
