#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldStaticLoadCalculator.h"

#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

constexpr float kFloatTolerance = 0.001f;

} // namespace

TEST(WorldStaticLoadCalculatorTest, ClearsStaticLoadWhenGravityIsZero)
{
    World world(5, 5);
    world.getPhysicsSettings().gravity = 0.0;
    world.addMaterialAtCell(2, 1, Material::EnumType::Dirt);
    world.getData().at(2, 1).static_load = 123.0f;

    WorldStaticLoadCalculator calculator;
    calculator.recomputeAll(world);

    EXPECT_NEAR(world.getData().at(2, 1).static_load, 0.0f, kFloatTolerance);
}

TEST(WorldStaticLoadCalculatorTest, RoutesVerticalGranularLoadDownward)
{
    World world(5, 6);
    world.getPhysicsSettings().gravity = 10.0;
    world.addMaterialAtCell(2, 1, Material::EnumType::Dirt);
    world.addMaterialAtCell(2, 2, Material::EnumType::Dirt);
    world.addMaterialAtCell(2, 3, Material::EnumType::Dirt);

    WorldStaticLoadCalculator calculator;
    calculator.recomputeAll(world);

    EXPECT_NEAR(world.getData().at(2, 1).static_load, 15.0f, kFloatTolerance);
    EXPECT_NEAR(world.getData().at(2, 2).static_load, 30.0f, kFloatTolerance);
    EXPECT_NEAR(world.getData().at(2, 3).static_load, 45.0f, kFloatTolerance);
}

TEST(WorldStaticLoadCalculatorTest, RoutesLoadToSingleDiagonalSupportWhenDirectSupportIsMissing)
{
    World world(5, 6);
    world.getPhysicsSettings().gravity = 10.0;
    world.addMaterialAtCell(2, 1, Material::EnumType::Dirt);
    world.addMaterialAtCell(1, 2, Material::EnumType::Dirt);

    WorldStaticLoadCalculator calculator;
    calculator.recomputeAll(world);

    EXPECT_NEAR(world.getData().at(2, 1).static_load, 15.0f, kFloatTolerance);
    EXPECT_NEAR(world.getData().at(1, 2).static_load, 30.0f, kFloatTolerance);
}

TEST(WorldStaticLoadCalculatorTest, StopsPropagationAtSupportSink)
{
    World world(5, 6);
    world.getPhysicsSettings().gravity = 10.0;
    world.addMaterialAtCell(2, 1, Material::EnumType::Dirt);
    world.addMaterialAtCell(2, 2, Material::EnumType::Dirt);
    world.addMaterialAtCell(2, 3, Material::EnumType::Wood);

    WorldStaticLoadCalculator calculator;
    calculator.recomputeAll(world);

    EXPECT_NEAR(world.getData().at(2, 1).static_load, 15.0f, kFloatTolerance);
    EXPECT_NEAR(world.getData().at(2, 2).static_load, 30.0f, kFloatTolerance);
    EXPECT_NEAR(world.getData().at(2, 3).static_load, 0.0f, kFloatTolerance);
}

TEST(WorldStaticLoadCalculatorTest, DoesNotAccumulateStaticLoadForWater)
{
    World world(5, 6);
    world.getPhysicsSettings().gravity = 10.0;
    world.addMaterialAtCell(2, 1, Material::EnumType::Water);
    world.addMaterialAtCell(2, 2, Material::EnumType::Dirt);

    WorldStaticLoadCalculator calculator;
    calculator.recomputeAll(world);

    EXPECT_NEAR(world.getData().at(2, 1).static_load, 0.0f, kFloatTolerance);
    EXPECT_NEAR(world.getData().at(2, 2).static_load, 15.0f, kFloatTolerance);
}
