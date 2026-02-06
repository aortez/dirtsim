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

    static std::pair<uint16_t, uint16_t> computePeerAdvertisementPorts(
        const OperatingSystemManager& manager)
    {
        return manager.computePeerAdvertisementPorts();
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
    OperatingSystemManager manager(
        OperatingSystemManager::TestMode{
            .dependencies = dependencies,
            .backendConfig = {},
            .hasBackendConfig = false,
        });

    const auto result = OperatingSystemManagerTestAccessor::runServiceCommand(
        manager, "start", "dirtsim-server.service");

    EXPECT_TRUE(result.isValue());
}

TEST(OperatingSystemManagerTest, RunServiceCommandReturnsErrorOnNonZeroExit)
{
    OperatingSystemManager::Dependencies dependencies;
    dependencies.systemCommand = [](const std::string&) { return 1 << 8; };
    OperatingSystemManager manager(
        OperatingSystemManager::TestMode{
            .dependencies = dependencies,
            .backendConfig = {},
            .hasBackendConfig = false,
        });

    const auto result = OperatingSystemManagerTestAccessor::runServiceCommand(
        manager, "restart", "dirtsim-ui.service");

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue().message, "systemctl restart failed for dirtsim-ui.service");
}

TEST(OperatingSystemManagerTest, RunServiceCommandReturnsErrorOnFailureToStart)
{
    OperatingSystemManager::Dependencies dependencies;
    dependencies.systemCommand = [](const std::string&) { return -1; };
    OperatingSystemManager manager(
        OperatingSystemManager::TestMode{
            .dependencies = dependencies,
            .backendConfig = {},
            .hasBackendConfig = false,
        });

    const auto result = OperatingSystemManagerTestAccessor::runServiceCommand(
        manager, "stop", "dirtsim-ui.service");

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue().message, "systemctl failed to start");
}

TEST(OperatingSystemManagerTest, PeerAdvertisementPortsDeriveFromBackendArgs)
{
    OperatingSystemManager::BackendConfig config;
    config.serverArgs = "-p 9001";
    config.uiArgs = "--port=7001";

    OperatingSystemManager manager(
        OperatingSystemManager::TestMode{
            .dependencies = {},
            .backendConfig = config,
            .hasBackendConfig = true,
        });

    const auto [serverPort, uiPort] =
        OperatingSystemManagerTestAccessor::computePeerAdvertisementPorts(manager);
    EXPECT_EQ(serverPort, 9001);
    EXPECT_EQ(uiPort, 7001);
}

TEST(OperatingSystemManagerTest, PeerAdvertisementPortsDefaultWhenArgsMissingOrInvalid)
{
    OperatingSystemManager::BackendConfig config;
    config.serverArgs = "--port=99999";
    config.uiArgs = "";

    OperatingSystemManager manager(
        OperatingSystemManager::TestMode{
            .dependencies = {},
            .backendConfig = config,
            .hasBackendConfig = true,
        });

    const auto [serverPort, uiPort] =
        OperatingSystemManagerTestAccessor::computePeerAdvertisementPorts(manager);
    EXPECT_EQ(serverPort, 8080);
    EXPECT_EQ(uiPort, 7070);
}
