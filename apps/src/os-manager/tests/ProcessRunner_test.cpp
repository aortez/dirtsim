#include "os-manager/ProcessRunner.h"
#include <gtest/gtest.h>

using namespace DirtSim::OsManager;

TEST(ProcessRunnerTest, CapturesOutputAndExitCode)
{
    const auto result = runProcessCapture({ "/bin/echo", "scanner-ok" }, 1000);

    ASSERT_TRUE(result.isValue());
    EXPECT_EQ(result.value().exitCode, 0);
    EXPECT_EQ(result.value().output, "scanner-ok\n");
}

TEST(ProcessRunnerTest, PreservesNonZeroExitCode)
{
    const auto result = runProcessCapture({ "/bin/sh", "-c", "exit 7" }, 1000);

    ASSERT_TRUE(result.isValue());
    EXPECT_EQ(result.value().exitCode, 7);
}

TEST(ProcessRunnerTest, TimesOutLongRunningProcess)
{
    const auto result = runProcessCapture({ "/bin/sleep", "1" }, 10);

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue(), "Process timed out");
}
