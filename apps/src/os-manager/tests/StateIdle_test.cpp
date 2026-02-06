#include "os-manager/OperatingSystemManager.h"
#include "os-manager/states/Idle.h"
#include "os-manager/states/State.h"
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

using namespace DirtSim;
using namespace DirtSim::OsManager;
using namespace DirtSim::OsManager::State;

class OsManagerStateIdleTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        dependencies.serviceCommand = [this](const std::string& action, const std::string& unit) {
            serviceCalls.emplace_back(action, unit);
            return Result<std::monostate, ApiError>::okay(std::monostate{});
        };
        dependencies.systemStatus = [this]() { return status; };
        manager = std::make_unique<OperatingSystemManager>(OperatingSystemManager::TestMode{
            .dependencies = dependencies,
            .backendConfig = {},
            .hasBackendConfig = false,
        });
    }

    OperatingSystemManager::Dependencies dependencies;
    std::vector<std::pair<std::string, std::string>> serviceCalls;
    OsApi::SystemStatus::Okay status;
    std::unique_ptr<OperatingSystemManager> manager;
};

TEST_F(OsManagerStateIdleTest, StartServerCallsServiceCommand)
{
    Idle idleState;
    bool callbackInvoked = false;

    OsApi::StartServer::Command cmd;
    OsApi::StartServer::Cwc cwc(cmd, [&](OsApi::StartServer::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_EQ(serviceCalls.size(), 1u);
    EXPECT_EQ(serviceCalls[0].first, "start");
    EXPECT_EQ(serviceCalls[0].second, "dirtsim-server.service");
}

TEST_F(OsManagerStateIdleTest, StartAudioCallsServiceCommand)
{
    Idle idleState;
    bool callbackInvoked = false;

    OsApi::StartAudio::Command cmd;
    OsApi::StartAudio::Cwc cwc(cmd, [&](OsApi::StartAudio::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_EQ(serviceCalls.size(), 1u);
    EXPECT_EQ(serviceCalls[0].first, "start");
    EXPECT_EQ(serviceCalls[0].second, "dirtsim-audio.service");
}

TEST_F(OsManagerStateIdleTest, RestartServerCallsServiceCommand)
{
    Idle idleState;
    bool callbackInvoked = false;

    OsApi::RestartServer::Command cmd;
    OsApi::RestartServer::Cwc cwc(cmd, [&](OsApi::RestartServer::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_EQ(serviceCalls.size(), 1u);
    EXPECT_EQ(serviceCalls[0].first, "restart");
    EXPECT_EQ(serviceCalls[0].second, "dirtsim-server.service");
}

TEST_F(OsManagerStateIdleTest, RestartAudioCallsServiceCommand)
{
    Idle idleState;
    bool callbackInvoked = false;

    OsApi::RestartAudio::Command cmd;
    OsApi::RestartAudio::Cwc cwc(cmd, [&](OsApi::RestartAudio::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_EQ(serviceCalls.size(), 1u);
    EXPECT_EQ(serviceCalls[0].first, "restart");
    EXPECT_EQ(serviceCalls[0].second, "dirtsim-audio.service");
}

TEST_F(OsManagerStateIdleTest, StopServerCallsServiceCommand)
{
    Idle idleState;
    bool callbackInvoked = false;

    OsApi::StopServer::Command cmd;
    OsApi::StopServer::Cwc cwc(cmd, [&](OsApi::StopServer::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_EQ(serviceCalls.size(), 1u);
    EXPECT_EQ(serviceCalls[0].first, "stop");
    EXPECT_EQ(serviceCalls[0].second, "dirtsim-server.service");
}

TEST_F(OsManagerStateIdleTest, StopAudioCallsServiceCommand)
{
    Idle idleState;
    bool callbackInvoked = false;

    OsApi::StopAudio::Command cmd;
    OsApi::StopAudio::Cwc cwc(cmd, [&](OsApi::StopAudio::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_EQ(serviceCalls.size(), 1u);
    EXPECT_EQ(serviceCalls[0].first, "stop");
    EXPECT_EQ(serviceCalls[0].second, "dirtsim-audio.service");
}

TEST_F(OsManagerStateIdleTest, StartUiCallsServiceCommand)
{
    Idle idleState;
    bool callbackInvoked = false;

    OsApi::StartUi::Command cmd;
    OsApi::StartUi::Cwc cwc(cmd, [&](OsApi::StartUi::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_EQ(serviceCalls.size(), 1u);
    EXPECT_EQ(serviceCalls[0].first, "start");
    EXPECT_EQ(serviceCalls[0].second, "dirtsim-ui.service");
}

TEST_F(OsManagerStateIdleTest, RestartUiCallsServiceCommand)
{
    Idle idleState;
    bool callbackInvoked = false;

    OsApi::RestartUi::Command cmd;
    OsApi::RestartUi::Cwc cwc(cmd, [&](OsApi::RestartUi::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_EQ(serviceCalls.size(), 1u);
    EXPECT_EQ(serviceCalls[0].first, "restart");
    EXPECT_EQ(serviceCalls[0].second, "dirtsim-ui.service");
}

TEST_F(OsManagerStateIdleTest, StopUiCallsServiceCommand)
{
    Idle idleState;
    bool callbackInvoked = false;

    OsApi::StopUi::Command cmd;
    OsApi::StopUi::Cwc cwc(cmd, [&](OsApi::StopUi::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_EQ(serviceCalls.size(), 1u);
    EXPECT_EQ(serviceCalls[0].first, "stop");
    EXPECT_EQ(serviceCalls[0].second, "dirtsim-ui.service");
}

TEST_F(OsManagerStateIdleTest, SystemStatusReturnsProvidedStatus)
{
    status.ui_status = "OK";
    status.server_status = "Error: unavailable";
    status.audio_status = "OK";

    Idle idleState;
    bool callbackInvoked = false;
    OsApi::SystemStatus::Response capturedResponse;

    OsApi::SystemStatus::Command cmd;
    OsApi::SystemStatus::Cwc cwc(cmd, [&](OsApi::SystemStatus::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(capturedResponse.isValue());
    EXPECT_EQ(capturedResponse.value().ui_status, "OK");
    EXPECT_EQ(capturedResponse.value().server_status, "Error: unavailable");
    EXPECT_EQ(capturedResponse.value().audio_status, "OK");
}

TEST_F(OsManagerStateIdleTest, ServiceCommandErrorPropagates)
{
    dependencies.serviceCommand = [](const std::string&, const std::string&) {
        return Result<std::monostate, ApiError>::error(ApiError("systemctl failed"));
    };
    manager = std::make_unique<OperatingSystemManager>(OperatingSystemManager::TestMode{
        .dependencies = dependencies,
        .backendConfig = {},
        .hasBackendConfig = false,
    });

    Idle idleState;
    bool callbackInvoked = false;
    OsApi::RestartServer::Response capturedResponse;

    OsApi::RestartServer::Command cmd;
    OsApi::RestartServer::Cwc cwc(cmd, [&](OsApi::RestartServer::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(capturedResponse.isError());
    EXPECT_EQ(capturedResponse.errorValue().message, "systemctl failed");
}

TEST_F(OsManagerStateIdleTest, RebootTransitionsToRebooting)
{
    Idle idleState;
    bool callbackInvoked = false;

    OsApi::Reboot::Command cmd;
    OsApi::Reboot::Cwc cwc(cmd, [&](OsApi::Reboot::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    State::Any newState = idleState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Rebooting>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
}
