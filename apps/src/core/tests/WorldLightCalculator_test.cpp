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
        // Default config with no ambient (so we can test sun clearly).
        config.ambient_color = ColorNames::black();
        config.sun_enabled = true;
        config.sun_color = ColorNames::white();
        config.sun_intensity = 1.0f;
        config.diffusion_iterations = 0;
        config.diffusion_rate = 0.0f;
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
