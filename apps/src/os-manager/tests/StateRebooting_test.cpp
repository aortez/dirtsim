#include "os-manager/OperatingSystemManager.h"
#include "os-manager/states/Rebooting.h"
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

using namespace DirtSim;
using namespace DirtSim::OsManager;
using namespace DirtSim::OsManager::State;

class OsManagerStateRebootingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        dependencies.serviceCommand = [this](const std::string& action, const std::string& unit) {
            serviceCalls.emplace_back(action, unit);
            return Result<std::monostate, ApiError>::okay(std::monostate{});
        };
        dependencies.systemStatus = [] { return OsApi::SystemStatus::Okay{}; };
        dependencies.reboot = [this]() { rebootRequested = true; };
        manager = std::make_unique<OperatingSystemManager>(
            OperatingSystemManager::TestMode{ dependencies });
    }

    OperatingSystemManager::Dependencies dependencies;
    std::vector<std::pair<std::string, std::string>> serviceCalls;
    bool rebootRequested = false;
    std::unique_ptr<OperatingSystemManager> manager;
};

TEST_F(OsManagerStateRebootingTest, StopsServicesAndRequestsReboot)
{
    Rebooting state;
    state.onEnter(*manager);

    ASSERT_TRUE(rebootRequested);
    ASSERT_TRUE(manager->shouldExit());
    ASSERT_EQ(serviceCalls.size(), 3u);
    EXPECT_EQ(serviceCalls[0].first, "stop");
    EXPECT_EQ(serviceCalls[0].second, "dirtsim-ui.service");
    EXPECT_EQ(serviceCalls[1].first, "stop");
    EXPECT_EQ(serviceCalls[1].second, "dirtsim-server.service");
    EXPECT_EQ(serviceCalls[2].first, "stop");
    EXPECT_EQ(serviceCalls[2].second, "dirtsim-audio.service");
}
