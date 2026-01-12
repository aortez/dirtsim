/**
 * @file WorldLightCalculator_test.cpp
 * @brief Tests for WorldLightCalculator.
 *
 * Tests cover:
 * - Sunlight in empty columns.
 * - Sunlight blocked by opaque materials.
 * - Emissive cells adding light.
 * - Diffusion softening shadows.
 * - ASCII light map visualization.
 */

#include "core/ColorNames.h"
#include "core/GridOfCells.h"
#include "core/LightConfig.h"
#include "core/MaterialType.h"
#include "core/PointLight.h"
#include "core/Timers.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldLightCalculator.h"
#include <gtest/gtest.h>

using namespace DirtSim;

class WorldLightCalculatorTest : public ::testing::Test {
protected:
    WorldLightCalculator calc;
    LightConfig config;
    ::Timers timers;

    void SetUp() override
    {
        // Explicit config for testing - no ambient, pure sunlight, no diffusion.
        config = {
            .ambient_color = ColorNames::black(),
            .ambient_intensity = 0.0f,
            .diffusion_iterations = 0,
            .diffusion_rate = 0.0f,
            .sky_access_enabled = false,
            .sky_access_falloff = 0.0f,
            .sun_color = ColorNames::white(),
            .sun_enabled = true,
            .sun_intensity = 1.0f,
        };
    }
};

TEST_F(WorldLightCalculatorTest, SunlightEmptyColumn)
{
    World world(10, 10);
    WorldData& data = world.getData();

    // Fill with WATER (low opacity 0.05) so light transmits through.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    calc.calculate(world, world.getGrid(), config, timers);

    // All cells should have some brightness (WATER is blue, attenuates slightly per row).
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            float brightness = ColorNames::brightness(data.colors.at(x, y));
            EXPECT_GT(brightness, 0.1f)
                << "Cell (" << x << "," << y << ") should be lit, got " << brightness;
        }
    }
}

TEST_F(WorldLightCalculatorTest, SunlightBlockedByWall)
{
    World world(10, 10);
    WorldData& data = world.getData();

    // Fill with WATER (low opacity) so we can see light differences.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Wall across row 3.
    for (int x = 0; x < 10; ++x) {
        data.at(x, 3).replaceMaterial(MaterialType::WALL, 1.0);
    }

    calc.calculate(world, world.getGrid(), config, timers);

    // Cells above wall (rows 0-2) should be lit (WATER is blue, ~0.26 brightness).
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 10; ++x) {
            float brightness = ColorNames::brightness(data.colors.at(x, y));
            EXPECT_GT(brightness, 0.2f)
                << "Cell (" << x << "," << y << ") above wall should be lit";
        }
    }

    // Cells below wall (rows 4-9) should be dark (no sun reaches them).
    for (int y = 4; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            float brightness = ColorNames::brightness(data.colors.at(x, y));
            EXPECT_LT(brightness, 0.1f)
                << "Cell (" << x << "," << y << ") below wall should be dark, got " << brightness;
        }
    }
}

TEST_F(WorldLightCalculatorTest, LeafPartiallyBlocksSunlight)
{
    World world(5, 10);
    WorldData& data = world.getData();

    // Fill with WATER (low opacity) so light transmits through.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Single leaf cell at x=2, y=0.
    data.at(2, 0).replaceMaterial(MaterialType::LEAF, 1.0);

    calc.calculate(world, world.getGrid(), config, timers);

    // Cell below leaf should be dimmer than cell in adjacent column.
    float below_leaf = ColorNames::brightness(data.colors.at(2, 5));
    float adjacent = ColorNames::brightness(data.colors.at(3, 5));

    EXPECT_LT(below_leaf, adjacent) << "Light below leaf should be dimmer than adjacent column";
    // Some light should still pass through (leaf opacity is 0.4).
    EXPECT_GT(below_leaf, 0.05f) << "Some light should pass through leaf";
}

TEST_F(WorldLightCalculatorTest, EmissiveSeedAddsLight)
{
    World world(5, 5);
    WorldData& data = world.getData();

    // Block all sun with a wall at top.
    for (int x = 0; x < 5; ++x) {
        data.at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // Place a seed in the dark area.
    data.at(2, 2).replaceMaterial(MaterialType::SEED, 1.0);

    calc.calculate(world, world.getGrid(), config, timers);

    // Seed cell should have some brightness from emission.
    float seed_brightness = ColorNames::brightness(data.colors.at(2, 2));
    EXPECT_GT(seed_brightness, 0.05f) << "Seed should emit some light";

    // Adjacent dark cell should have no light (no diffusion enabled).
    float adjacent_brightness = ColorNames::brightness(data.colors.at(3, 3));
    EXPECT_LT(adjacent_brightness, 0.01f) << "Non-emissive dark cell should remain dark";
}

TEST_F(WorldLightCalculatorTest, WaterTransmitsLight)
{
    World world(10, 10);
    WorldData& data = world.getData();

    // Fill with WATER (opacity=0.05, scatter=0.1) - transparent enough for light.
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Block sun on right half with wall.
    for (int y = 0; y < 10; ++y) {
        for (int x = 5; x < 10; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WALL, 1.0);
        }
    }

    calc.calculate(world, world.getGrid(), config, timers);

    // Water in lit area should have brightness (WATER is blue, ~0.26 brightness).
    float water_brightness = ColorNames::brightness(data.colors.at(2, 5));
    EXPECT_GT(water_brightness, 0.2f) << "Water should transmit sunlight";

    // Water at boundary (x=4) still receives sunlight.
    float boundary_brightness = ColorNames::brightness(data.colors.at(4, 5));
    EXPECT_GT(boundary_brightness, 0.2f) << "Boundary water should be lit";
}

TEST_F(WorldLightCalculatorTest, LightMapStringProducesOutput)
{
    World world(10, 5);
    WorldData& data = world.getData();

    // Fill with WATER so cells are visible.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    calc.calculate(world, world.getGrid(), config, timers);

    std::string light_map = calc.lightMapString(world);

    // Should have content.
    EXPECT_FALSE(light_map.empty());

    // Should have correct number of lines.
    size_t newline_count = std::count(light_map.begin(), light_map.end(), '\n');
    EXPECT_EQ(newline_count, 5u) << "Should have 5 lines for 5 rows";

    // With full sun on WATER, should have some non-space characters.
    size_t space_chars = std::count(light_map.begin(), light_map.end(), ' ');
    EXPECT_LT(space_chars, 50u) << "Most cells should have some brightness";
}

TEST_F(WorldLightCalculatorTest, AmbientLightAddsBaseIllumination)
{
    World world(5, 5);
    WorldData& data = world.getData();

    // Fill with SAND so we can see light.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::SAND, 1.0);
        }
    }

    // Block all sun.
    for (int x = 0; x < 5; ++x) {
        data.at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // Disable sun to isolate ambient light test.
    config.sun_enabled = false;

    // No ambient (disable sky access for uniform ambient test).
    config.ambient_color = ColorNames::black();
    config.ambient_intensity = 1.0f;
    config.sky_access_enabled = false;
    calc.calculate(world, world.getGrid(), config, timers);
    float dark_brightness = ColorNames::brightness(data.colors.at(2, 3));

    // With ambient.
    config.ambient_color = 0x404040FF; // Gray ambient.
    config.ambient_intensity = 1.0f;
    calc.calculate(world, world.getGrid(), config, timers);
    float ambient_brightness = ColorNames::brightness(data.colors.at(2, 3));

    EXPECT_GT(ambient_brightness, dark_brightness) << "Ambient light should add base illumination";
}

TEST_F(WorldLightCalculatorTest, AmbientIntensityScalesLight)
{
    World world(5, 5);
    WorldData& data = world.getData();

    // Fill with WATER so we can see light.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Disable sun to isolate ambient.
    config.sun_enabled = false;
    config.ambient_color = 0x808080FF; // Mid-gray.
    config.sky_access_enabled = false; // Uniform ambient for this test.

    // Intensity 1.0.
    config.ambient_intensity = 1.0f;
    calc.calculate(world, world.getGrid(), config, timers);
    float brightness_1x = ColorNames::brightness(data.colors.at(2, 2));

    // Intensity 2.0.
    config.ambient_intensity = 2.0f;
    calc.calculate(world, world.getGrid(), config, timers);
    float brightness_2x = ColorNames::brightness(data.colors.at(2, 2));

    EXPECT_GT(brightness_2x, brightness_1x) << "Higher ambient intensity should be brighter";
}

TEST_F(WorldLightCalculatorTest, SkyAccessAttenuatesUnderground)
{
    World world(5, 10);
    WorldData& data = world.getData();

    // Fill with WATER (low opacity, visible color).
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Wall at row 3 blocks sky access.
    for (int x = 0; x < 5; ++x) {
        data.at(x, 3).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // Disable sun, use only ambient with sky access.
    config.sun_enabled = false;
    config.ambient_color = 0xFFFFFFFF; // White ambient.
    config.ambient_intensity = 1.0f;
    config.sky_access_enabled = true;
    config.sky_access_falloff = 1.0f;

    calc.calculate(world, world.getGrid(), config, timers);

    // Cell above wall (row 2) should have decent ambient.
    float above_wall = ColorNames::brightness(data.colors.at(2, 2));
    // Cell below wall (row 5) should have reduced ambient.
    float below_wall = ColorNames::brightness(data.colors.at(2, 5));

    EXPECT_GT(above_wall, 0.2f) << "Cell above wall should have some ambient";
    EXPECT_LT(below_wall, above_wall) << "Cell below wall should have less ambient (sky blocked)";
}

TEST_F(WorldLightCalculatorTest, SkyAccessVerticalShaft)
{
    World world(10, 10);
    WorldData& data = world.getData();

    // Fill with WATER (low opacity, visible color).
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Wall across row 2, but leave a gap at x=5 (vertical shaft).
    for (int x = 0; x < 10; ++x) {
        if (x != 5) {
            data.at(x, 2).replaceMaterial(MaterialType::WALL, 1.0);
        }
    }

    // Disable sun, use only ambient with sky access.
    config.sun_enabled = false;
    config.ambient_color = 0xFFFFFFFF;
    config.ambient_intensity = 1.0f;
    config.sky_access_enabled = true;
    config.sky_access_falloff = 1.0f;

    calc.calculate(world, world.getGrid(), config, timers);

    // Cell below wall (blocked) should be dimmer.
    float blocked = ColorNames::brightness(data.colors.at(3, 5));
    // Cell in vertical shaft should maintain higher ambient.
    float shaft = ColorNames::brightness(data.colors.at(5, 5));

    EXPECT_GT(shaft, blocked) << "Cell in shaft should be brighter than blocked cell";
    EXPECT_GT(shaft, 0.2f) << "Cell in vertical shaft should have decent ambient";
}

// =============================================================================
// Point Light Tests
// =============================================================================

TEST_F(WorldLightCalculatorTest, PointLightIlluminatesDarkRoom)
{
    World world(20, 20);
    WorldData& data = world.getData();

    // Fill with WATER so cells are visible.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Wall at top blocks all sun.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // Disable sun and ambient.
    config.sun_enabled = false;
    config.ambient_color = ColorNames::black();
    config.sky_access_enabled = false;

    // Add a point light in the center.
    PointLight torch;
    torch.position = Vector2d{ 10.0, 10.0 };
    torch.color = ColorNames::white();
    torch.intensity = 1.0f;
    torch.radius = 8.0f;
    torch.attenuation = 0.1f;
    world.addPointLight(torch);

    calc.calculate(world, world.getGrid(), config, timers);

    // Cell at light source should be bright.
    float center_brightness = ColorNames::brightness(data.colors.at(10, 10));
    EXPECT_GT(center_brightness, 0.5f) << "Cell at light source should be bright";

    // Cell at edge of radius should be dimmer.
    float edge_brightness = ColorNames::brightness(data.colors.at(17, 10));
    EXPECT_LT(edge_brightness, center_brightness) << "Edge should be dimmer than center";
    EXPECT_GT(edge_brightness, 0.01f) << "Edge should still have some light";

    // Cell outside radius should be dark.
    float outside_brightness = ColorNames::brightness(data.colors.at(1, 10));
    EXPECT_LT(outside_brightness, 0.01f) << "Cell outside radius should be dark";
}

TEST_F(WorldLightCalculatorTest, PointLightFalloffWithDistance)
{
    World world(20, 10);
    WorldData& data = world.getData();

    // Fill with WATER.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Block sun.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
    }

    config.sun_enabled = false;
    config.ambient_color = ColorNames::black();
    config.sky_access_enabled = false;

    // Point light at left side.
    PointLight light;
    light.position = Vector2d{ 5.0, 5.0 };
    light.color = ColorNames::white();
    light.intensity = 1.0f;
    light.radius = 15.0f;
    light.attenuation = 0.1f;
    world.addPointLight(light);

    calc.calculate(world, world.getGrid(), config, timers);

    // Measure brightness at increasing distances.
    float dist_0 = ColorNames::brightness(data.colors.at(5, 5));
    float dist_3 = ColorNames::brightness(data.colors.at(8, 5));
    float dist_6 = ColorNames::brightness(data.colors.at(11, 5));

    EXPECT_GT(dist_0, dist_3) << "Brightness should decrease with distance";
    EXPECT_GT(dist_3, dist_6) << "Brightness should continue decreasing";
}

TEST_F(WorldLightCalculatorTest, PointLightBlockedByWall)
{
    World world(20, 10);
    WorldData& data = world.getData();

    // Fill with WATER.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Block sun.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // Vertical wall in the middle.
    for (int y = 2; y < 8; ++y) {
        data.at(10, y).replaceMaterial(MaterialType::WALL, 1.0);
    }

    config.sun_enabled = false;
    config.ambient_color = ColorNames::black();
    config.sky_access_enabled = false;

    // Point light on left side of wall.
    PointLight light;
    light.position = Vector2d{ 5.0, 5.0 };
    light.color = ColorNames::white();
    light.intensity = 1.0f;
    light.radius = 15.0f;
    light.attenuation = 0.05f;
    world.addPointLight(light);

    calc.calculate(world, world.getGrid(), config, timers);

    // Cell on same side as light should be lit.
    float same_side = ColorNames::brightness(data.colors.at(8, 5));
    EXPECT_GT(same_side, 0.1f) << "Cell on same side as light should be lit";

    // Cell behind wall should be dark (shadow).
    float behind_wall = ColorNames::brightness(data.colors.at(12, 5));
    EXPECT_LT(behind_wall, same_side * 0.1f) << "Cell behind wall should be in shadow";
}

TEST_F(WorldLightCalculatorTest, MultiplePointLightsAdditive)
{
    World world(20, 10);
    WorldData& data = world.getData();

    // Fill with WATER.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Block sun.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
    }

    config.sun_enabled = false;
    config.ambient_color = ColorNames::black();
    config.sky_access_enabled = false;

    // Single light.
    PointLight light1;
    light1.position = Vector2d{ 5.0, 5.0 };
    light1.color = ColorNames::white();
    light1.intensity = 1.0f;
    light1.radius = 10.0f;
    light1.attenuation = 0.1f;
    world.addPointLight(light1);

    calc.calculate(world, world.getGrid(), config, timers);
    float one_light = ColorNames::brightness(data.colors.at(10, 5));

    // Clear and add two lights.
    world.clearPointLights();
    world.addPointLight(light1);
    PointLight light2;
    light2.position = Vector2d{ 15.0, 5.0 };
    light2.color = ColorNames::white();
    light2.intensity = 1.0f;
    light2.radius = 10.0f;
    light2.attenuation = 0.1f;
    world.addPointLight(light2);

    calc.calculate(world, world.getGrid(), config, timers);
    float two_lights = ColorNames::brightness(data.colors.at(10, 5));

    EXPECT_GT(two_lights, one_light) << "Two lights should be brighter than one";
}

// =============================================================================
// Air Scattering Tests
// =============================================================================

TEST_F(WorldLightCalculatorTest, AirScatteringDiffusesLightSideways)
{
    // Test that air scattering allows light to reach shadowed areas.
    // Compare with/without diffusion to prove air scattering works.
    World world(20, 10);
    WorldData& data = world.getData();

    // Wall covering right side at row 0 (blocks sun for x >= 8).
    for (int x = 8; x < 20; ++x) {
        data.at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // SAND marker in shadow (x=10, just past shadow boundary).
    data.at(10, 5).replaceMaterial(MaterialType::SAND, 1.0);

    // First: calculate WITHOUT diffusion.
    config.diffusion_iterations = 0;
    config.diffusion_rate = 0.0f;
    calc.calculate(world, world.getGrid(), config, timers);

    float no_diffusion = ColorNames::brightness(data.colors.at(10, 5));

    // Second: calculate WITH diffusion (air scattering).
    config.diffusion_iterations = 5;
    config.diffusion_rate = 0.5f;
    calc.calculate(world, world.getGrid(), config, timers);

    float with_diffusion = ColorNames::brightness(data.colors.at(10, 5));

    // Without diffusion, shadow should be dark (wall blocks direct sun).
    EXPECT_LT(no_diffusion, 0.1f) << "Without diffusion, shadow should be dark";

    // With diffusion (air scattering), shadow should receive some light.
    EXPECT_GT(with_diffusion, no_diffusion) << "Air scattering should bring more light into shadow";
}

TEST_F(WorldLightCalculatorTest, AirScatteringSoftensOverhangShadow)
{
    // Test that air scattering brings light under an overhang.
    // Compare with/without diffusion.
    World world(15, 12);
    WorldData& data = world.getData();

    // Create an overhang: wall at y=4, extending from x=6 to edge.
    for (int x = 6; x < 15; ++x) {
        data.at(x, 4).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // WATER marker under overhang (x=10, y=6).
    data.at(10, 6).replaceMaterial(MaterialType::WATER, 1.0);

    // First: calculate WITHOUT diffusion.
    config.diffusion_iterations = 0;
    config.diffusion_rate = 0.0f;
    calc.calculate(world, world.getGrid(), config, timers);

    float no_diffusion = ColorNames::brightness(data.colors.at(10, 6));

    // Second: calculate WITH diffusion (air scattering).
    config.diffusion_iterations = 5;
    config.diffusion_rate = 0.5f;
    calc.calculate(world, world.getGrid(), config, timers);

    float with_diffusion = ColorNames::brightness(data.colors.at(10, 6));

    // Without diffusion, under overhang should be dark.
    EXPECT_LT(no_diffusion, 0.1f) << "Without diffusion, under overhang should be dark";

    // With diffusion (air scattering), light should reach under overhang.
    EXPECT_GT(with_diffusion, no_diffusion) << "Air scattering should bring light under overhang";
}
