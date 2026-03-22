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
    world.setBulkWaterAmountAtCell(2, 1, 1.0f);
    world.addMaterialAtCell(2, 2, Material::EnumType::Dirt);

    WorldStaticLoadCalculator calculator;
    calculator.recomputeAll(world);

    EXPECT_NEAR(world.getData().at(2, 1).static_load, 0.0f, kFloatTolerance);
    EXPECT_NEAR(world.getData().at(2, 2).static_load, 15.0f, kFloatTolerance);
}

TEST(WorldStaticLoadCalculatorTest, SupportedBuriedGranularCellSkipsGravityOnFirstStep)
{
    World world(5, 6);
    PhysicsSettings& settings = world.getPhysicsSettings();
    settings.adhesion_enabled = false;
    settings.adhesion_strength = 0.0;
    settings.cohesion_enabled = false;
    settings.cohesion_strength = 0.0;
    settings.friction_enabled = false;
    settings.friction_strength = 0.0;
    settings.gravity = 10.0;
    settings.pressure_decay_rate = 0.0;
    settings.pressure_diffusion_iterations = 0;
    settings.pressure_diffusion_strength = 0.0;
    settings.pressure_dynamic_enabled = false;
    settings.pressure_dynamic_strength = 0.0;
    settings.pressure_hydrostatic_enabled = false;
    settings.pressure_hydrostatic_strength = 0.0;
    settings.pressure_scale = 0.0;
    settings.swap_enabled = false;
    settings.viscosity_enabled = false;
    settings.viscosity_strength = 0.0;
    world.setAirResistanceEnabled(false);
    world.setAirResistanceStrength(0.0);

    world.addMaterialAtCell(2, 1, Material::EnumType::Dirt);
    world.addMaterialAtCell(2, 2, Material::EnumType::Dirt);
    world.addMaterialAtCell(2, 3, Material::EnumType::Dirt);
    world.addMaterialAtCell(2, 4, Material::EnumType::Wood);

    world.advanceTime(0.016);

    const WorldData& data = world.getData();
    const size_t idx = static_cast<size_t>(2) * data.width + 2;
    const CellDebug& debug = data.debug_info[idx];
    EXPECT_TRUE(debug.carries_transmitted_granular_load);
    EXPECT_TRUE(debug.gravity_skipped_for_support);
    EXPECT_TRUE(debug.has_granular_support_path);
    EXPECT_NEAR(data.at(2, 2).velocity.y, 0.0f, kFloatTolerance);
}
