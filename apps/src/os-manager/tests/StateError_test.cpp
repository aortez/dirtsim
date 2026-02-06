#include "os-manager/OperatingSystemManager.h"
#include "os-manager/states/Error.h"
#include "os-manager/states/State.h"
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

using namespace DirtSim;
using namespace DirtSim::OsManager;
using namespace DirtSim::OsManager::State;

class OsManagerStateErrorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        dependencies.serviceCommand = [](const std::string&, const std::string&) {
            return Result<std::monostate, ApiError>::okay(std::monostate{});
        };
        dependencies.systemStatus = [this]() { return status; };
        dependencies.reboot = [] {};
        manager = std::make_unique<OperatingSystemManager>(OperatingSystemManager::TestMode{
            .dependencies = dependencies,
            .backendConfig = {},
            .hasBackendConfig = false,
        });
    }

    OperatingSystemManager::Dependencies dependencies;
    OsApi::SystemStatus::Okay status;
    std::unique_ptr<OperatingSystemManager> manager;
};

TEST_F(OsManagerStateErrorTest, SystemStatusReturnsProvidedStatus)
{
    status.ui_status = "OK";
    status.server_status = "OK";
    status.audio_status = "OK";

    Error errorState;
    errorState.error_message = "test-error";

    bool callbackInvoked = false;
    OsApi::SystemStatus::Response capturedResponse;

    OsApi::SystemStatus::Command cmd;
    OsApi::SystemStatus::Cwc cwc(cmd, [&](OsApi::SystemStatus::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    State::Any newState = errorState.onEvent(cwc, *manager);

    ASSERT_TRUE(std::holds_alternative<Error>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(capturedResponse.isValue());
    EXPECT_EQ(capturedResponse.value().ui_status, "OK");
    EXPECT_EQ(capturedResponse.value().server_status, "OK");
    EXPECT_EQ(capturedResponse.value().audio_status, "OK");
}
