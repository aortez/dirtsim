/**
 * @file ColorTuning_test.cpp
 * @brief Tests for color tuning and material rendering under various lighting.
 *
 * This test provides a controlled environment to verify how materials appear
 * under different lighting conditions. The world is stepped minimally so
 * physics doesn't move materials, but lighting fully propagates.
 *
 * Layout (20x20 world, matching LightsScenario):
 *   - Bottom row: Water (0-4), Metal (5-9), Leaf (10-14), Dirt (15-19)
 *   - Each material block is 5x5 at y=15-19
 *   - Rest is air (empty)
 *
 * Color inspection helpers print actual RGB values for tuning.
 */

#include "core/Cell.h"
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
#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>

using namespace DirtSim;

namespace {

// Convert RgbF to hex string for easy reading.
std::string rgbToHex(const ColorNames::RgbF& c)
{
    uint32_t rgba = ColorNames::toRgba(c);
    std::ostringstream oss;
    oss << "#" << std::hex << std::setfill('0') << std::setw(6) << (rgba >> 8);
    return oss.str();
}

// Print color info for a specific cell.
void printCellColor(const WorldData& data, int x, int y, const std::string& label)
{
    if (!data.inBounds(x, y)) {
        std::cout << label << " (" << x << "," << y << "): OUT OF BOUNDS\n";
        return;
    }

    const Cell& cell = data.at(x, y);
    const ColorNames::RgbF& color = data.colors.at(x, y);

    std::cout << label << " (" << x << "," << y
              << "): " << "material=" << static_cast<int>(cell.material_type) << " "
              << "fill=" << std::fixed << std::setprecision(2) << cell.fill_ratio << " " << "rgb=("
              << std::setprecision(3) << color.r << "," << color.g << "," << color.b << ") "
              << "hex=" << rgbToHex(color) << " " << "brightness=" << std::setprecision(3)
              << ColorNames::brightness(color) << "\n";
}

// Print a grid of colors as hex values.
void printColorGrid(const WorldData& data, int startX, int startY, int w, int h)
{
    std::cout << "\nColor grid (" << startX << "," << startY << ") to (" << startX + w - 1 << ","
              << startY + h - 1 << "):\n";
    for (int y = startY; y < startY + h && y < data.height; ++y) {
        std::cout << "  y=" << std::setw(2) << y << ": ";
        for (int x = startX; x < startX + w && x < data.width; ++x) {
            if (data.inBounds(x, y)) {
                std::cout << rgbToHex(data.colors.at(x, y)) << " ";
            }
        }
        std::cout << "\n";
    }
}

// Print material swatch summary.
// Surface row (y=15) shows lit colors, depth row (y=17) shows absorption.
void printSwatchSummary(const WorldData& data)
{
    std::cout << "\n=== Material Swatch Summary (Surface y=15) ===\n";
    printCellColor(data, 2, 15, "Water ");
    printCellColor(data, 7, 15, "Metal ");
    printCellColor(data, 12, 15, "Leaf  ");
    printCellColor(data, 17, 15, "Dirt  ");
    printCellColor(data, 10, 5, "Air   ");
    std::cout << "\n=== Material Swatch Summary (Depth y=17) ===\n";
    printCellColor(data, 2, 17, "Water ");
    printCellColor(data, 7, 17, "Metal ");
    printCellColor(data, 12, 17, "Leaf  ");
    printCellColor(data, 17, 17, "Dirt  ");
    std::cout << "=============================================\n\n";
}

} // namespace

class ColorTuningTest : public ::testing::Test {
protected:
    WorldLightCalculator calc;
    LightConfig config;
    ::Timers timers;

    void SetUp() override
    {
        // Full light config with sun enabled.
        config = {
            .air_scatter_rate = 0.15f,
            .ambient_color = ColorNames::dayAmbient(),
            .ambient_intensity = 0.3f,
            .diffusion_iterations = 3,
            .diffusion_rate = 0.3f,
            .sky_access_enabled = true,
            .sky_access_falloff = 0.5f,
            .sky_access_multi_directional = false,
            .sun_color = ColorNames::warmSunlight(),
            .sun_enabled = true,
            .sun_intensity = 1.0f,
        };
    }

    // Set up the standard test world (matches LightsScenario layout).
    void setupTestWorld(World& world)
    {
        WorldData& data = world.getData();

        // Clear world.
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                data.at(x, y) = Cell();
            }
        }

        // Water block (x=0-4, y=15-19).
        for (int y = 15; y <= 19; ++y) {
            for (int x = 0; x <= 4; ++x) {
                if (data.inBounds(x, y)) {
                    data.at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
                }
            }
        }

        // Metal block (x=5-9, y=15-19).
        for (int y = 15; y <= 19; ++y) {
            for (int x = 5; x <= 9; ++x) {
                if (data.inBounds(x, y)) {
                    data.at(x, y).replaceMaterial(Material::EnumType::Metal, 1.0);
                }
            }
        }

        // Leaf block (x=10-14, y=15-19).
        for (int y = 15; y <= 19; ++y) {
            for (int x = 10; x <= 14; ++x) {
                if (data.inBounds(x, y)) {
                    data.at(x, y).replaceMaterial(Material::EnumType::Leaf, 1.0);
                }
            }
        }

        // Dirt block (x=15-19, y=15-19).
        for (int y = 15; y <= 19; ++y) {
            for (int x = 15; x <= 19; ++x) {
                if (data.inBounds(x, y)) {
                    data.at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0);
                }
            }
        }

        // Tiny physics step to initialize grid cache.
        world.advanceTime(0.0001);
    }
};

TEST_F(ColorTuningTest, MaterialColorsUnderSunlight)
{
    World world(20, 20);
    setupTestWorld(world);

    // Calculate lighting.
    calc.calculate(world, world.getGrid(), config, timers);

    // Print swatch summary for visual inspection.
    printSwatchSummary(world.getData());

    // Verify each material has distinct, non-zero colors at surface (y=15).
    // We check surface row because deeper cells absorb light.
    const WorldData& data = world.getData();

    ColorNames::RgbF water_color = data.colors.at(2, 15);
    ColorNames::RgbF metal_color = data.colors.at(7, 15);
    ColorNames::RgbF leaf_color = data.colors.at(12, 15);
    ColorNames::RgbF dirt_color = data.colors.at(17, 15);

    // All materials at surface should have good brightness under sunlight.
    EXPECT_GT(ColorNames::brightness(water_color), 0.3f) << "Water surface should be lit";
    EXPECT_GT(ColorNames::brightness(metal_color), 0.3f) << "Metal surface should be lit";
    EXPECT_GT(ColorNames::brightness(leaf_color), 0.3f) << "Leaf surface should be lit";
    EXPECT_GT(ColorNames::brightness(dirt_color), 0.3f) << "Dirt surface should be lit";

    // Water should be bluish (more blue than red).
    EXPECT_GT(water_color.b, water_color.r) << "Water should be bluish";

    // Leaf should be greenish (more green than red or blue).
    EXPECT_GT(leaf_color.g, leaf_color.r) << "Leaf should be greenish";
    EXPECT_GT(leaf_color.g, leaf_color.b) << "Leaf should be greenish";

    // Metal should be relatively gray (all channels similar).
    float metal_diff = std::max(
        { std::abs(metal_color.r - metal_color.g),
          std::abs(metal_color.g - metal_color.b),
          std::abs(metal_color.r - metal_color.b) });
    EXPECT_LT(metal_diff, 0.15f) << "Metal should be grayish";
}

TEST_F(ColorTuningTest, PrintColorGridForTuning)
{
    World world(20, 20);
    setupTestWorld(world);

    calc.calculate(world, world.getGrid(), config, timers);

    // Print the material row colors for visual tuning.
    std::cout << "\n=== Color Grid for Tuning ===\n";
    printColorGrid(world.getData(), 0, 15, 20, 5);

    // This test always passes - it's for visual output.
    SUCCEED();
}

TEST_F(ColorTuningTest, SunColorAffectsOutput)
{
    World world(20, 20);
    setupTestWorld(world);

    // Warm sunlight.
    config.sun_color = ColorNames::warmSunlight();
    calc.calculate(world, world.getGrid(), config, timers);
    ColorNames::RgbF metal_warm = world.getData().colors.at(7, 17);

    // Cool moonlight.
    config.sun_color = ColorNames::coolMoonlight();
    calc.calculate(world, world.getGrid(), config, timers);
    ColorNames::RgbF metal_cool = world.getData().colors.at(7, 17);

    std::cout << "\n=== Sun Color Comparison (Metal) ===\n";
    std::cout << "Warm sun: " << rgbToHex(metal_warm) << " (r=" << metal_warm.r << ")\n";
    std::cout << "Cool moon: " << rgbToHex(metal_cool) << " (r=" << metal_cool.r << ")\n";

    // Warm sunlight should produce more red than cool moonlight.
    EXPECT_GT(metal_warm.r, metal_cool.r) << "Warm sun should have more red";
    // Cool moonlight should produce more blue.
    EXPECT_GT(metal_cool.b, metal_warm.b) << "Cool moon should have more blue";
}

TEST_F(ColorTuningTest, AmbientAffectsShadowedAreas)
{
    World world(20, 20);
    setupTestWorld(world);

    // Add a wall to create shadow.
    WorldData& data = world.getData();
    for (int x = 5; x < 15; ++x) {
        data.at(x, 10).replaceMaterial(Material::EnumType::Wall, 1.0);
    }
    world.advanceTime(0.0001);

    // Low ambient.
    config.ambient_intensity = 0.1f;
    calc.calculate(world, world.getGrid(), config, timers);
    ColorNames::RgbF shadow_low = data.colors.at(10, 12);

    // High ambient.
    config.ambient_intensity = 0.8f;
    calc.calculate(world, world.getGrid(), config, timers);
    ColorNames::RgbF shadow_high = data.colors.at(10, 12);

    std::cout << "\n=== Ambient Effect on Shadow ===\n";
    std::cout << "Low ambient (0.1): " << rgbToHex(shadow_low)
              << " brightness=" << ColorNames::brightness(shadow_low) << "\n";
    std::cout << "High ambient (0.8): " << rgbToHex(shadow_high)
              << " brightness=" << ColorNames::brightness(shadow_high) << "\n";

    EXPECT_GT(ColorNames::brightness(shadow_high), ColorNames::brightness(shadow_low))
        << "Higher ambient should brighten shadows";
}

TEST_F(ColorTuningTest, PointLightColorTinting)
{
    World world(20, 20);
    setupTestWorld(world);

    // Disable sun to isolate point light effect.
    config.sun_enabled = false;
    config.ambient_intensity = 0.0f;

    // Add orange point light above materials.
    PointLight torch;
    torch.position = Vector2d{ 10.0, 12.0 };
    torch.color = ColorNames::torchOrange();
    torch.intensity = 2.0f;
    torch.radius = 15.0f;
    torch.attenuation = 0.05f;
    world.getLightManager().addLight(torch);

    calc.calculate(world, world.getGrid(), config, timers);

    std::cout << "\n=== Point Light Tinting (Torch Orange) ===\n";
    printSwatchSummary(world.getData());

    // Under orange torch light, materials should have warm tint.
    // Check surface row (y=15) which is closer to the torch at y=12.
    ColorNames::RgbF leaf_color = world.getData().colors.at(12, 15);

    // Surface leaf should have some orange influence (elevated red).
    EXPECT_GT(leaf_color.r, 0.05f) << "Leaf surface should have some red from torch";
}
