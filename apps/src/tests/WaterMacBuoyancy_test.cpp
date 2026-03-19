#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"

#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

Cell* findFirstCellOfType(World& world, Material::EnumType type)
{
    WorldData& data = world.getData();
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.material_type == type) {
                return &cell;
            }
        }
    }
    return nullptr;
}

} // namespace

TEST(WaterMacBuoyancyTest, WoodRisesAndDirtSinks)
{
    constexpr int kWidth = 20;
    constexpr int kHeight = 20;
    constexpr double kDeltaTime = 0.02;

    World world(kWidth, kHeight);

    PhysicsSettings& settings = world.getPhysicsSettings();
    settings.water_sim_mode = WaterSimMode::MacProjection;
    settings.mac_water_buoyancy_strength = 1.0;
    settings.mac_water_drag_rate = 0.0;
    settings.cohesion_strength = 0.0;
    settings.adhesion_strength = 0.0;
    settings.friction_strength = 0.0;
    settings.pressure_hydrostatic_strength = 0.0;
    settings.pressure_dynamic_strength = 0.0;
    settings.pressure_diffusion_strength = 0.0;
    settings.viscosity_strength = 0.0;

    world.setAirResistanceEnabled(false);

    constexpr int kWaterTop = 8;
    for (int y = kWaterTop; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            world.addMaterialAtCell(x, y, Material::EnumType::Water, 1.0f);
        }
    }

    constexpr int kWoodX = 10;
    constexpr int kWoodY = 15;
    constexpr int kDirtX = 12;
    constexpr int kDirtY = 15;

    world.getData().at(kWoodX, kWoodY).replaceMaterial(Material::EnumType::Wood, 1.0f);
    world.getData().at(kDirtX, kDirtY).replaceMaterial(Material::EnumType::Dirt, 1.0f);

    world.advanceTime(kDeltaTime);

    Cell* woodCell = findFirstCellOfType(world, Material::EnumType::Wood);
    ASSERT_NE(woodCell, nullptr);
    Cell* dirtCell = findFirstCellOfType(world, Material::EnumType::Dirt);
    ASSERT_NE(dirtCell, nullptr);

    EXPECT_LT(woodCell->velocity.y, 0.0f);
    EXPECT_GT(dirtCell->velocity.y, 0.0f);
}
