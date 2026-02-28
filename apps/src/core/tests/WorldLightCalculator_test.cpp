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
#include "core/LightManager.h"
#include "core/LightTypes.h"
#include "core/MaterialType.h"
#include "core/Timers.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldLightCalculator.h"
#include <cmath>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

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
            .air_scatter_rate = 0.15f,
            .ambient_color = ColorNames::black(),
            .ambient_intensity = 0.0f,
            .diffusion_iterations = 0,
            .diffusion_rate = 0.0f,
            .sky_access_enabled = false,
            .sky_access_falloff = 0.0f,
            .sky_access_multi_directional = false,
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
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
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
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
        }
    }

    // Wall across row 3.
    for (int x = 0; x < 10; ++x) {
        data.at(x, 3).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    // Advance to rebuild grid cache after placing materials.
    world.advanceTime(0.0001);

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
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
        }
    }

    // Single leaf cell at x=2, y=0.
    data.at(2, 0).replaceMaterial(Material::EnumType::Leaf, 1.0);

    // Advance to rebuild grid cache after placing materials.
    world.advanceTime(0.0001);

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
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    // Place a seed in the dark area.
    data.at(2, 2).replaceMaterial(Material::EnumType::Seed, 1.0);

    // Advance to rebuild grid cache after placing materials.
    world.advanceTime(0.0001);

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
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
        }
    }

    // Block sun on right half with wall.
    for (int y = 0; y < 10; ++y) {
        for (int x = 5; x < 10; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Wall, 1.0);
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
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
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
            data.at(x, y).replaceMaterial(Material::EnumType::Sand, 1.0);
        }
    }

    // Block all sun.
    for (int x = 0; x < 5; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
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
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
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
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
        }
    }

    // Wall at row 3 blocks sky access.
    for (int x = 0; x < 5; ++x) {
        data.at(x, 3).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    // Advance to rebuild grid cache after placing materials.
    world.advanceTime(0.0001);

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
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
        }
    }

    // Wall across row 2, but leave a gap at x=5 (vertical shaft).
    for (int x = 0; x < 10; ++x) {
        if (x != 5) {
            data.at(x, 2).replaceMaterial(Material::EnumType::Wall, 1.0);
        }
    }

    // Advance to rebuild grid cache after placing materials.
    world.advanceTime(0.0001);

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

TEST_F(WorldLightCalculatorTest, SkyAccessMultiDirectionalCreatesAtoBtoCFalloff)
{
    World world(21, 9);
    WorldData& data = world.getData();

    // Start with all AIR to isolate ambient sky behavior.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).clear();
        }
    }

    // Roof with center opening.
    for (int x = 0; x <= 5; ++x) {
        data.at(x, 4).replaceMaterial(Material::EnumType::Wall, 1.0f);
    }
    for (int x = 14; x < data.width; ++x) {
        data.at(x, 4).replaceMaterial(Material::EnumType::Wall, 1.0f);
    }

    // Side walls.
    for (int y = 5; y <= 7; ++y) {
        data.at(0, y).replaceMaterial(Material::EnumType::Wall, 1.0f);
        data.at(data.width - 1, y).replaceMaterial(Material::EnumType::Wall, 1.0f);
    }

    // Floor.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 8).replaceMaterial(Material::EnumType::Wall, 1.0f);
    }

    // Advance to rebuild grid cache after placing materials.
    world.advanceTime(0.0001);

    config.sun_enabled = false;
    config.ambient_color = ColorNames::white();
    config.ambient_intensity = 1.0f;
    config.sky_access_enabled = true;
    config.sky_access_falloff = 1.0f;
    config.sky_access_multi_directional = true;
    calc.calculate(world, world.getGrid(), config, timers);

    // Print lightmap with sample points for debugging.
    spdlog::info("=== SkyAccessMultiDirectionalCreatesAtoBtoCFalloff Lightmap ===");
    spdlog::info("Legend: X=wall, shades dark->bright, a/b/c are sampled cells");
    std::string lightmap = calc.lightMapString(world);
    std::istringstream iss(lightmap);
    std::string line;
    int row = 0;
    while (std::getline(iss, line)) {
        if (row == 7 && line.size() > 10) {
            line[5] = 'b';
            line[10] = 'a';
        }
        if (row == 5 && line.size() > 18) {
            line[18] = 'c';
        }
        spdlog::info("{:2d}: {}", row++, line);
    }

    // a: Directly under opening (vertical and diagonal sky access).
    const float a = ColorNames::brightness(data.colors.at(10, 7));
    // b: Under roof edge (blocked vertically, visible through one diagonal probe).
    const float b = ColorNames::brightness(data.colors.at(5, 7));
    // c: Deep side pocket (blocked vertically and by both diagonals).
    const float c = ColorNames::brightness(data.colors.at(18, 5));
    spdlog::info("a(10,7)={:.3f}, b(5,7)={:.3f}, c(18,5)={:.3f}", a, b, c);

    EXPECT_GT(a, b) << "a should be brighter than b with direct sky access";
    EXPECT_GT(b, c) << "b should be brighter than c with one diagonal path";
    EXPECT_GT(a, 0.95f) << "a should be near full ambient";
    EXPECT_GT(b, 0.20f) << "b should receive measurable diagonal ambient";
    EXPECT_LT(b, 0.30f) << "b should be dimmer than direct-lit cells";
    EXPECT_LT(c, 0.05f) << "c should be near dark with no probe path";
}

TEST_F(WorldLightCalculatorTest, SkyAccessMultiDirectionalNumericalAccuracy)
{
    // Verify exact sky_factor values for two hand-calculable scenarios.
    //
    // Part 1 – All-AIR world: every cell has sky_factor = 1.0, so the result must
    // match uniform ambient (sky_access_enabled=false) for every cell.
    //
    // Part 2 – Opaque wall at row 1, world 5 wide × 4 tall:
    //   Probe weights: 0.5 vertical (V), 0.25 upper-left (UL), 0.25 upper-right (UR).
    //   Row 0 (above wall): all probes exit the top → sky_factor = 1.0.
    //   Row 2+ interior (0 < x < 4): V hits wall, UL hits wall, UR hits wall → 0.0.
    //   Row 2+ left edge (x=0): V hits wall, UL exits world left (=1.0), UR hits wall
    //     → sky_factor = 0.5×0 + 0.25×1 + 0.25×0 = 0.25.
    //   Row 2+ right edge (x=4): symmetric → sky_factor = 0.25.

    // --- Part 1: all-AIR world ---
    {
        World world(6, 5);
        WorldData& data = world.getData();
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                data.at(x, y).clear();
            }
        }

        config.sun_enabled = false;
        config.ambient_color = ColorNames::white();
        config.ambient_intensity = 1.0f;
        config.sky_access_falloff = 1.0f;
        config.diffusion_iterations = 0;
        config.diffusion_rate = 0.0f;

        // Uniform ambient (no sky access).
        config.sky_access_enabled = false;
        calc.calculate(world, world.getGrid(), config, timers);
        const float uniform_brightness = ColorNames::brightness(data.colors.at(3, 2));

        // Multi-directional sky access on all-AIR world should give identical brightness.
        config.sky_access_enabled = true;
        config.sky_access_multi_directional = true;
        calc.calculate(world, world.getGrid(), config, timers);
        const float sky_brightness = ColorNames::brightness(data.colors.at(3, 2));

        ASSERT_GT(uniform_brightness, 0.0f) << "Uniform ambient should light the cell.";
        EXPECT_NEAR(sky_brightness, uniform_brightness, uniform_brightness * 0.01f)
            << "All-AIR world: multi-directional sky access should equal uniform ambient "
               "(sky_factor=1.0 everywhere).";
    }

    // --- Part 2: opaque wall at row 1 ---
    {
        World world(5, 4);
        WorldData& data = world.getData();

        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                data.at(x, y).clear();
            }
        }
        for (int x = 0; x < data.width; ++x) {
            data.at(x, 1).replaceMaterial(Material::EnumType::Wall, 1.0f);
        }
        world.advanceTime(0.0001);

        config.sun_enabled = false;
        config.ambient_color = ColorNames::white();
        config.ambient_intensity = 1.0f;
        config.sky_access_enabled = true;
        config.sky_access_falloff = 1.0f;
        config.sky_access_multi_directional = true;
        config.diffusion_iterations = 0;
        config.diffusion_rate = 0.0f;
        calc.calculate(world, world.getGrid(), config, timers);

        // Reference brightness for cells with full sky access (row 0, all same material).
        const float ref = ColorNames::brightness(data.colors.at(2, 0));
        ASSERT_GT(ref, 0.0f) << "Row-0 cells must be lit (sky_factor=1.0).";

        // Row 2 (immediately below the wall): all three probes hit the wall before
        // exiting the world for every interior cell → sky_factor = 0.0.
        // At deeper rows the diagonal probes can escape around the wall edges, so
        // only row 2 is guaranteed to be fully blocked for interior cells.
        {
            constexpr int y = 2;
            for (int x = 1; x < data.width - 1; ++x) {
                const float b = ColorNames::brightness(data.colors.at(x, y));
                EXPECT_LT(b, ref * 0.01f)
                    << "Interior cell (" << x << "," << y
                    << ") immediately below opaque wall must be dark (sky_factor=0).";
            }
        }

        // Left-edge cells (x=0): UL probe exits world left → sky_factor = 0.25.
        for (int y = 2; y < data.height; ++y) {
            const float b = ColorNames::brightness(data.colors.at(0, y));
            EXPECT_NEAR(b, ref * 0.25f, ref * 0.02f)
                << "Left-edge cell (0," << y
                << ") below opaque wall must have sky_factor ≈ 0.25 "
                   "(UL probe exits world, V and UR blocked by wall).";
        }

        // Right-edge cells (x=W-1): UR probe exits world right → sky_factor = 0.25.
        for (int y = 2; y < data.height; ++y) {
            const float b = ColorNames::brightness(data.colors.at(data.width - 1, y));
            EXPECT_NEAR(b, ref * 0.25f, ref * 0.02f)
                << "Right-edge cell (" << (data.width - 1) << "," << y
                << ") below opaque wall must have sky_factor ≈ 0.25 "
                   "(UR probe exits world, V and UL blocked by wall).";
        }
    }
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
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
        }
    }

    // Wall at top blocks all sun.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
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
    world.getLightManager().addLight(torch);

    calc.calculate(world, world.getGrid(), config, timers);

    // Print lightmap for debugging.
    spdlog::info("=== PointLightIlluminatesDarkRoom Lightmap ===");
    spdlog::info("Light at (10,10), radius=8. Shades: ' '=dark, '@'=bright");
    std::string lightmap = calc.lightMapString(world);
    std::istringstream iss(lightmap);
    std::string line;
    int row = 0;
    while (std::getline(iss, line)) {
        spdlog::info("{:2d}: {}", row++, line);
    }

    // Cell at light source should be bright.
    float center_brightness = ColorNames::brightness(data.colors.at(10, 10));
    spdlog::info(
        "center(10,10)={:.4f}, edge(17,10)={:.4f}, outside(1,10)={:.4f}",
        center_brightness,
        ColorNames::brightness(data.colors.at(17, 10)),
        ColorNames::brightness(data.colors.at(1, 10)));
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
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
        }
    }

    // Block sun.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
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
    world.getLightManager().addLight(light);

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
    World world(15, 10);
    WorldData& data = world.getData();

    // Block sun at top.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    // Small wall stub in the lit area - just 3 cells at x=8, rows 5-7.
    for (int y = 5; y <= 7; ++y) {
        data.at(8, y).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    // Advance to rebuild grid cache after placing materials.
    world.advanceTime(0.0001);

    config.sun_enabled = false;
    config.ambient_color = ColorNames::black();
    config.sky_access_enabled = false;

    // Point light on left side of wall.
    PointLight light;
    light.position = Vector2d{ 5.0, 5.0 };
    light.color = ColorNames::white();
    light.intensity = 1.0f;
    light.radius = 12.0f;
    light.attenuation = 0.08f;
    world.getLightManager().addLight(light);

    calc.calculate(world, world.getGrid(), config, timers);

    // Print lightmap for debugging.
    spdlog::info("=== PointLightBlockedByWall Lightmap ===");
    spdlog::info(
        "Light at (5,5), wall stub at x=8 rows 5-7. Shades: ' '=dark, 'W'=wall, '@'=bright");
    std::string lightmap = calc.lightMapString(world);
    std::istringstream iss(lightmap);
    std::string line;
    int row = 0;
    while (std::getline(iss, line)) {
        spdlog::info("{:2d}: {}", row++, line);
    }

    // Cell on light side of wall should be lit.
    float light_side = ColorNames::brightness(data.colors.at(7, 6));
    EXPECT_GT(light_side, 0.1f) << "Cell on light side of wall should be lit";

    // Cell in shadow behind wall should be dark.
    float shadow_side = ColorNames::brightness(data.colors.at(9, 6));
    EXPECT_LT(shadow_side, light_side * 0.5f) << "Cell behind wall should be in shadow";

    // Cell past the wall (row 4, no wall there) should still be lit.
    float past_wall = ColorNames::brightness(data.colors.at(9, 4));
    EXPECT_GT(past_wall, shadow_side) << "Cell past wall end should receive light";
}

TEST_F(WorldLightCalculatorTest, MultiplePointLightsAdditive)
{
    World world(20, 10);
    WorldData& data = world.getData();

    // Fill with WATER.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
        }
    }

    // Block sun.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
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
    world.getLightManager().addLight(light1);

    calc.calculate(world, world.getGrid(), config, timers);
    float one_light = ColorNames::brightness(data.colors.at(10, 5));

    // Clear and add two lights.
    world.getLightManager().clear();
    world.getLightManager().addLight(light1);
    PointLight light2;
    light2.position = Vector2d{ 15.0, 5.0 };
    light2.color = ColorNames::white();
    light2.intensity = 1.0f;
    light2.radius = 10.0f;
    light2.attenuation = 0.1f;
    world.getLightManager().addLight(light2);

    calc.calculate(world, world.getGrid(), config, timers);
    float two_lights = ColorNames::brightness(data.colors.at(10, 5));

    EXPECT_GT(two_lights, one_light) << "Two lights should be brighter than one";
}

TEST_F(WorldLightCalculatorTest, PointLightSpreadIsCircular)
{
    // Verify that light spreads equally in all directions, not biased toward cardinal axes.
    // A + shaped light pattern indicates broken diagonal propagation.
    World world(21, 21);
    WorldData& data = world.getData();

    // Fill with WATER so cells are visible.
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
        }
    }

    // Wall at top blocks all sun.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    // Disable sun, ambient, and diffusion to isolate point light behavior.
    config.sun_enabled = false;
    config.ambient_color = ColorNames::black();
    config.sky_access_enabled = false;
    config.diffusion_iterations = 0;
    config.diffusion_rate = 0.0f;

    // Point light at center (10, 10).
    PointLight light;
    light.position = Vector2d{ 10.0, 10.0 };
    light.color = ColorNames::white();
    light.intensity = 1.0f;
    light.radius = 10.0f;
    light.attenuation = 0.1f;
    world.getLightManager().addLight(light);

    // Rebuild grid cache.
    world.advanceTime(0.0001);

    calc.calculate(world, world.getGrid(), config, timers);

    // Print lightmap for debugging.
    spdlog::info("=== PointLightSpreadIsCircular Lightmap ===");
    std::string lightmap = calc.lightMapString(world);
    std::istringstream iss(lightmap);
    std::string line;
    int row = 0;
    while (std::getline(iss, line)) {
        spdlog::info("{:2d}: {}", row++, line);
    }

    // Measure brightness at equal distances from center in different directions.
    // Distance 5: cardinal points.
    float cardinal_right = ColorNames::brightness(data.colors.at(15, 10)); // (10+5, 10)
    float cardinal_down = ColorNames::brightness(data.colors.at(10, 15));  // (10, 10+5)

    // Distance ~5: diagonal points (3.54 * sqrt(2) ≈ 5).
    // Using offset (4, 4) gives distance sqrt(32) ≈ 5.66, close enough.
    // Using offset (3, 4) gives distance 5.0 exactly.
    float diagonal_se = ColorNames::brightness(data.colors.at(13, 14)); // (10+3, 10+4), dist=5
    float diagonal_sw = ColorNames::brightness(data.colors.at(7, 14));  // (10-3, 10+4), dist=5

    spdlog::info(
        "Cardinal: right(15,10)={:.4f}, down(10,15)={:.4f}", cardinal_right, cardinal_down);
    spdlog::info("Diagonal: SE(13,14)={:.4f}, SW(7,14)={:.4f}", diagonal_se, diagonal_sw);

    // All points at same distance should have similar brightness.
    // Allow 20% tolerance for discrete grid effects.
    float avg_cardinal = (cardinal_right + cardinal_down) / 2.0f;
    float avg_diagonal = (diagonal_se + diagonal_sw) / 2.0f;
    spdlog::info(
        "avg_cardinal={:.4f}, avg_diagonal={:.4f}, ratio={:.2f}%",
        avg_cardinal,
        avg_diagonal,
        100.0f * avg_diagonal / avg_cardinal);

    // Diagonal should be at least 80% as bright as cardinal at same distance.
    EXPECT_GT(avg_diagonal, avg_cardinal * 0.8f)
        << "Diagonal cells should be nearly as bright as cardinal cells at same distance. "
        << "Cardinal avg: " << avg_cardinal << ", Diagonal avg: " << avg_diagonal
        << ". A large difference indicates + shaped (cardinal-biased) light spread.";

    // Also verify both are reasonably lit (not zero).
    EXPECT_GT(avg_cardinal, 0.05f) << "Cardinal cells should receive light";
    EXPECT_GT(avg_diagonal, 0.05f) << "Diagonal cells should receive light";
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
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    // SAND marker in shadow (x=10, just past shadow boundary).
    data.at(10, 5).replaceMaterial(Material::EnumType::Sand, 1.0);

    // Advance to rebuild grid cache after placing materials.
    world.advanceTime(0.0001);

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
        data.at(x, 4).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    // WATER marker under overhang (x=10, y=6).
    data.at(10, 6).replaceMaterial(Material::EnumType::Water, 1.0);

    // Advance to rebuild grid cache after placing materials.
    world.advanceTime(0.0001);

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

// =============================================================================
// SpotLight Arc Tests (Parameterized)
// =============================================================================

struct SpotLightTestCase {
    std::string name;
    float direction;                   // Radians from +x axis.
    float arc_width;                   // Arc width in radians.
    std::vector<Vector2i> expect_lit;  // Positions that should be lit.
    std::vector<Vector2i> expect_dark; // Positions that should be dark.
};

class SpotLightArcTest : public ::testing::TestWithParam<SpotLightTestCase> {
protected:
    WorldLightCalculator calc;
    LightConfig config;
    ::Timers timers;

    void SetUp() override
    {
        config = {
            .air_scatter_rate = 0.0f,
            .ambient_color = ColorNames::black(),
            .ambient_intensity = 0.0f,
            .diffusion_iterations = 0,
            .diffusion_rate = 0.0f,
            .sky_access_enabled = false,
            .sky_access_falloff = 0.0f,
            .sky_access_multi_directional = false,
            .sun_color = ColorNames::white(),
            .sun_enabled = false,
            .sun_intensity = 0.0f,
        };
    }
};

TEST_P(SpotLightArcTest, ArcIlluminatesExpectedRegions)
{
    const auto& tc = GetParam();

    World world(21, 21);
    WorldData& data = world.getData();

    // Block sun at top.
    for (int x = 0; x < data.width; ++x) {
        data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    world.advanceTime(0.0001);

    // SpotLight at center.
    SpotLight spot;
    spot.position = Vector2f{ 10.0f, 10.0f };
    spot.color = ColorNames::white();
    spot.intensity = 1.0f;
    spot.radius = 12.0f;
    spot.attenuation = 0.08f;
    spot.direction = tc.direction;
    spot.arc_width = tc.arc_width;
    world.getLightManager().addLight(spot);

    calc.calculate(world, world.getGrid(), config, timers);

    // Print lightmap for debugging.
    spdlog::info("=== SpotLightArcTest: {} ===", tc.name);
    spdlog::info(
        "direction={:.1f}° arc_width={:.1f}°",
        tc.direction * 180.0f / M_PI,
        tc.arc_width * 180.0f / M_PI);
    std::string lightmap = calc.lightMapString(world);
    std::istringstream iss(lightmap);
    std::string line;
    int row = 0;
    while (std::getline(iss, line)) {
        spdlog::info("{:2d}: {}", row++, line);
    }

    // Check expected lit positions.
    for (const auto& pos : tc.expect_lit) {
        float brightness = ColorNames::brightness(data.colors.at(pos.x, pos.y));
        EXPECT_GT(brightness, 0.05f)
            << tc.name << ": Position (" << pos.x << "," << pos.y << ") should be LIT";
    }

    // Check expected dark positions.
    for (const auto& pos : tc.expect_dark) {
        float brightness = ColorNames::brightness(data.colors.at(pos.x, pos.y));
        EXPECT_LT(brightness, 0.02f)
            << tc.name << ": Position (" << pos.x << "," << pos.y << ") should be DARK";
    }
}

INSTANTIATE_TEST_SUITE_P(
    ArcConfigurations,
    SpotLightArcTest,
    ::testing::Values(
        // 30° arc facing right - narrow beam.
        SpotLightTestCase{ .name = "30deg_facing_right",
                           .direction = 0.0f,
                           .arc_width = static_cast<float>(30.0 * M_PI / 180.0),
                           .expect_lit = { { 14, 10 } },                          // Right.
                           .expect_dark = { { 6, 10 }, { 10, 6 }, { 10, 14 } } }, // Left, up, down.

        // 180° arc facing right - right hemisphere lit.
        SpotLightTestCase{
            .name = "180deg_facing_right",
            .direction = 0.0f,
            .arc_width = static_cast<float>(M_PI),
            .expect_lit = { { 14, 10 }, { 13, 7 }, { 13, 13 } }, // Right, up-right, down-right.
            .expect_dark = { { 6, 10 } } },                      // Left.

        // 280° arc with gap in top-left quadrant (screen coords: y-down).
        // Top-left on screen is ~225° (up-left), so direction points opposite at 45°.
        SpotLightTestCase{
            .name = "280deg_gap_topleft",
            .direction = static_cast<float>(M_PI / 4.0),
            .arc_width = static_cast<float>(280.0 * M_PI / 180.0),
            .expect_lit = { { 14, 10 }, { 6, 10 }, { 10, 14 } }, // Right, left, down.
            .expect_dark = { { 7, 7 } } }                        // Top-left diagonal (in gap).
        ),
    [](const ::testing::TestParamInfo<SpotLightTestCase>& info) { return info.param.name; });
