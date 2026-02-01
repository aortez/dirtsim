#include "os-manager/OperatingSystemManager.h"
#include <gtest/gtest.h>
#include <string>

using namespace DirtSim;
using namespace DirtSim::OsManager;

namespace DirtSim {
namespace OsManager {

struct OperatingSystemManagerTestAccessor {
    static OsApi::SystemStatus::Okay buildSystemStatusInternal(OperatingSystemManager& manager)
    {
        return manager.buildSystemStatusInternal();
    }

    static Result<std::monostate, ApiError> runServiceCommand(
        OperatingSystemManager& manager, const std::string& action, const std::string& unitName)
    {
        return manager.runServiceCommand(action, unitName);
    }
};

} // namespace OsManager
} // namespace DirtSim

namespace {

bool isOkOrError(const std::string& status)
{
    return status == "OK" || status.rfind("Error:", 0) == 0;
}

} // namespace

TEST(OperatingSystemManagerTest, BuildSystemStatusInternalReportsMetricsAndHealth)
{
    OperatingSystemManager manager(0);

    const auto status = OperatingSystemManagerTestAccessor::buildSystemStatusInternal(manager);

    EXPECT_GT(status.memory_total_kb, 0u);
    EXPECT_GT(status.disk_total_bytes_root, 0u);
    EXPECT_FALSE(status.server_status.empty());
    EXPECT_FALSE(status.ui_status.empty());
    EXPECT_FALSE(status.audio_status.empty());
    EXPECT_TRUE(isOkOrError(status.server_status));
    EXPECT_TRUE(isOkOrError(status.ui_status));
    EXPECT_TRUE(isOkOrError(status.audio_status));
}

TEST(OperatingSystemManagerTest, RunServiceCommandReturnsOkayOnZeroExit)
{
    OperatingSystemManager::Dependencies dependencies;
    dependencies.systemCommand = [](const std::string&) { return 0; };
    OperatingSystemManager manager(OperatingSystemManager::TestMode{ dependencies });

    const auto result = OperatingSystemManagerTestAccessor::runServiceCommand(
        manager, "start", "dirtsim-server.service");

    EXPECT_TRUE(result.isValue());
}

TEST(OperatingSystemManagerTest, RunServiceCommandReturnsErrorOnNonZeroExit)
{
    OperatingSystemManager::Dependencies dependencies;
    dependencies.systemCommand = [](const std::string&) { return 1 << 8; };
    OperatingSystemManager manager(OperatingSystemManager::TestMode{ dependencies });

    const auto result = OperatingSystemManagerTestAccessor::runServiceCommand(
        manager, "restart", "dirtsim-ui.service");

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue().message, "systemctl restart failed for dirtsim-ui.service");
}

TEST(OperatingSystemManagerTest, RunServiceCommandReturnsErrorOnFailureToStart)
{
    OperatingSystemManager::Dependencies dependencies;
    dependencies.systemCommand = [](const std::string&) { return -1; };
    OperatingSystemManager manager(OperatingSystemManager::TestMode{ dependencies });

    const auto result = OperatingSystemManagerTestAccessor::runServiceCommand(
        manager, "stop", "dirtsim-ui.service");

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue().message, "systemctl failed to start");
}
