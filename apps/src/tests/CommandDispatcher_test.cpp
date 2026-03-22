#include "cli/CommandDispatcher.h"

#include <gtest/gtest.h>

using namespace DirtSim::Client;

TEST(CommandDispatcherTest, ServerRegistersExplicitBulkWaterCommands)
{
    const DirtSim::Client::CommandDispatcher dispatcher;

    EXPECT_TRUE(dispatcher.hasCommand(Target::Server, "BulkWaterSet"));
    EXPECT_TRUE(dispatcher.hasCommand(Target::Server, "SpawnWaterBall"));

    const auto bulkWaterSetExample = dispatcher.getExample(Target::Server, "BulkWaterSet");
    ASSERT_TRUE(bulkWaterSetExample.isValue());
    ASSERT_TRUE(bulkWaterSetExample.value().is_object());
    EXPECT_EQ(bulkWaterSetExample.value().at("x"), 0);
    EXPECT_EQ(bulkWaterSetExample.value().at("y"), 0);
    EXPECT_EQ(bulkWaterSetExample.value().at("amount"), 1.0);

    const auto spawnWaterBallExample = dispatcher.getExample(Target::Server, "SpawnWaterBall");
    ASSERT_TRUE(spawnWaterBallExample.isValue());
    EXPECT_TRUE(spawnWaterBallExample.value().is_null());
}
