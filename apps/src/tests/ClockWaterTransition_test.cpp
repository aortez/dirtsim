#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/scenarios/clock_scenario/ClockEventTypes.h"
#include "core/scenarios/clock_scenario/DrainManager.h"
#include "core/scenarios/clock_scenario/MeltdownEvent.h"

#include <gtest/gtest.h>
#include <optional>

using namespace DirtSim;

TEST(ClockWaterTransitionTest, DrainOpensForBulkWaterOnBottomPlayableRow)
{
    World world(11, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    DrainManager drain;
    const int bottomRow = world.getData().height - 2;
    const int centerX = world.getData().width / 2;
    world.addBulkWaterAtCell(centerX, bottomRow, 1.0f);

    drain.update(world, 0.1, 0.0, std::nullopt);

    EXPECT_TRUE(drain.isOpen());
    EXPECT_EQ(drain.getStartX(), centerX);
    EXPECT_EQ(drain.getEndX(), centerX);
}

TEST(ClockWaterTransitionTest, DrainDissipatesBulkWaterAtOpenDrainCells)
{
    World world(11, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    DrainManager drain;
    drain.update(world, 0.1, 1.0, std::nullopt);
    ASSERT_TRUE(drain.isOpen());

    const int drainX = drain.getStartX();
    const int drainY = world.getData().height - 1;
    world.setBulkWaterAmountAtCell(drainX, drainY, 1.0f);

    drain.update(world, 0.05, 1.0, std::nullopt);

    EXPECT_NEAR(world.getBulkWaterAmountAtCell(drainX, drainY), 0.5f, 0.001f);
}

TEST(ClockWaterTransitionTest, MeltdownConvertsFallenDigitsToBulkWaterInMacMode)
{
    World world(9, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    MeltdownEventState state{};
    state.digit_material = Material::EnumType::Metal;

    const int aboveBottomY = world.getData().height - 2;
    const int bottomWallY = world.getData().height - 1;
    world.getData().at(3, aboveBottomY).replaceMaterial(Material::EnumType::Metal, 0.75f);
    world.getData().at(4, bottomWallY).replaceMaterial(Material::EnumType::Metal, 0.5f);

    double remainingTime = 10.0;
    ClockEvents::updateMeltdown(state, world, remainingTime, 20.0, true, 4, 4);

    EXPECT_TRUE(world.getData().at(3, aboveBottomY).isEmpty());
    EXPECT_TRUE(world.getData().at(4, bottomWallY).isEmpty());
    EXPECT_NEAR(world.getBulkWaterAmountAtCell(3, aboveBottomY), 0.75f, 0.001f);
    EXPECT_NEAR(world.getBulkWaterAmountAtCell(4, bottomWallY), 0.5f, 0.001f);
}

TEST(ClockWaterTransitionTest, MeltdownEndConvertsRemainingDigitsToBulkWaterInMacMode)
{
    World world(9, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    world.getData().at(2, 2).replaceMaterial(Material::EnumType::Metal, 0.4f);
    world.getData().at(5, 4).replaceMaterial(Material::EnumType::Metal, 0.9f);

    ClockEvents::endMeltdown(world, Material::EnumType::Metal);

    EXPECT_TRUE(world.getData().at(2, 2).isEmpty());
    EXPECT_TRUE(world.getData().at(5, 4).isEmpty());
    EXPECT_NEAR(world.getBulkWaterAmountAtCell(2, 2), 0.4f, 0.001f);
    EXPECT_NEAR(world.getBulkWaterAmountAtCell(5, 4), 0.9f, 0.001f);
}
