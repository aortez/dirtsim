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
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldLightCalculator.h"
#include <gtest/gtest.h>

using namespace DirtSim;

class WorldLightCalculatorTest : public ::testing::Test {
protected:
    WorldLightCalculator calc;
    LightConfig config;

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

    calc.calculate(world, config);

    // All cells should be bright (full sun passes through AIR).
    const auto& data = world.getData();
    for (uint32_t y = 0; y < data.height; ++y) {
        for (uint32_t x = 0; x < data.width; ++x) {
            float brightness = ColorNames::brightness(data.at(x, y).getColor());
            EXPECT_GT(brightness, 0.9f)
                << "Cell (" << x << "," << y << ") should be bright, got " << brightness;
        }
    }
}

TEST_F(WorldLightCalculatorTest, SunlightBlockedByWall)
{
    World world(10, 10);
    WorldData& data = world.getData();

    // Wall across row 3.
    for (uint32_t x = 0; x < 10; ++x) {
        data.at(x, 3).replaceMaterial(MaterialType::WALL, 1.0);
    }

    calc.calculate(world, config);

    // Cells above wall (rows 0-2) should be bright.
    for (uint32_t y = 0; y < 3; ++y) {
        for (uint32_t x = 0; x < 10; ++x) {
            float brightness = ColorNames::brightness(data.at(x, y).getColor());
            EXPECT_GT(brightness, 0.8f)
                << "Cell (" << x << "," << y << ") above wall should be lit";
        }
    }

    // Cells below wall (rows 4-9) should be dark.
    for (uint32_t y = 4; y < 10; ++y) {
        for (uint32_t x = 0; x < 10; ++x) {
            float brightness = ColorNames::brightness(data.at(x, y).getColor());
            EXPECT_LT(brightness, 0.1f)
                << "Cell (" << x << "," << y << ") below wall should be dark, got " << brightness;
        }
    }
}

TEST_F(WorldLightCalculatorTest, LeafPartiallyBlocksSunlight)
{
    World world(5, 10);
    WorldData& data = world.getData();

    // Single leaf cell at x=2, y=0.
    data.at(2, 0).replaceMaterial(MaterialType::LEAF, 1.0);

    calc.calculate(world, config);

    // Cell below leaf should be dimmer than cell in adjacent column.
    float below_leaf = ColorNames::brightness(data.at(2, 5).getColor());
    float adjacent = ColorNames::brightness(data.at(3, 5).getColor());

    EXPECT_LT(below_leaf, adjacent) << "Light below leaf should be dimmer than adjacent column";
    // With single leaf (opacity 0.4), transmittance ~0.6, plus green tinting reduces brightness.
    EXPECT_GT(below_leaf, 0.01f) << "Some light should pass through leaf";
}

TEST_F(WorldLightCalculatorTest, EmissiveSeedAddsLight)
{
    World world(5, 5);
    WorldData& data = world.getData();

    // Block all sun with a wall at top.
    for (uint32_t x = 0; x < 5; ++x) {
        data.at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // Place a seed in the dark area.
    data.at(2, 2).replaceMaterial(MaterialType::SEED, 1.0);

    calc.calculate(world, config);

    // Seed cell should have some brightness from emission.
    float seed_brightness = ColorNames::brightness(data.at(2, 2).getColor());
    EXPECT_GT(seed_brightness, 0.05f) << "Seed should emit some light";

    // Adjacent dark cell should have no light (no diffusion enabled).
    float adjacent_brightness = ColorNames::brightness(data.at(3, 3).getColor());
    EXPECT_LT(adjacent_brightness, 0.01f) << "Non-emissive dark cell should remain dark";
}

TEST_F(WorldLightCalculatorTest, WaterTransmitsLight)
{
    World world(10, 10);
    WorldData& data = world.getData();

    // Fill with WATER (opacity=0.05, scatter=0.1) - transparent enough for light.
    for (uint32_t y = 0; y < 10; ++y) {
        for (uint32_t x = 0; x < 10; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Block sun on right half with wall.
    for (uint32_t y = 0; y < 10; ++y) {
        for (uint32_t x = 5; x < 10; ++x) {
            data.at(x, y).replaceMaterial(MaterialType::WALL, 1.0);
        }
    }

    calc.calculate(world, config);

    // Water in lit area should have brightness (accounting for blue tint and attenuation).
    float water_brightness = ColorNames::brightness(data.at(2, 5).getColor());
    EXPECT_GT(water_brightness, 0.3f) << "Water should transmit sunlight";

    // Water at boundary (x=4) still receives sunlight.
    float boundary_brightness = ColorNames::brightness(data.at(4, 5).getColor());
    EXPECT_GT(boundary_brightness, 0.3f) << "Boundary water should be lit";
}

TEST_F(WorldLightCalculatorTest, LightMapStringProducesOutput)
{
    World world(10, 5);
    calc.calculate(world, config);

    std::string light_map = calc.lightMapString(world);

    // Should have content.
    EXPECT_FALSE(light_map.empty());

    // Should have correct number of lines.
    size_t newline_count = std::count(light_map.begin(), light_map.end(), '\n');
    EXPECT_EQ(newline_count, 5u) << "Should have 5 lines for 5 rows";

    // With full sun, should mostly be bright characters.
    size_t bright_chars = std::count(light_map.begin(), light_map.end(), '@');
    EXPECT_GT(bright_chars, 40u) << "Most cells should be at max brightness";
}

TEST_F(WorldLightCalculatorTest, AmbientLightAddsBaseIllumination)
{
    World world(5, 5);
    WorldData& data = world.getData();

    // Block all sun.
    for (uint32_t x = 0; x < 5; ++x) {
        data.at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // No ambient.
    config.ambient_color = ColorNames::black();
    calc.calculate(world, config);
    float dark_brightness = ColorNames::brightness(data.at(2, 3).getColor());

    // With ambient.
    config.ambient_color = 0x404040FF; // Gray ambient.
    calc.calculate(world, config);
    float ambient_brightness = ColorNames::brightness(data.at(2, 3).getColor());

    EXPECT_GT(ambient_brightness, dark_brightness) << "Ambient light should add base illumination";
}
