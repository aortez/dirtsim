/**
 * @file DuckLedgeStability_test.cpp
 * @brief Integration tests for duck holding a flashlight on a ledge.
 *
 * Tests verify:
 * - Flashlight reaches horizontal equilibrium when duck stands still.
 * - Flashlight responds to duck acceleration during jumps.
 */

#include "DuckTestUtils.h"
#include "core/ColorNames.h"
#include "core/LightConfig.h"
#include "core/LightManager.h"
#include "core/LightTypes.h"
#include "core/Timers.h"
#include "core/World.h"
#include "core/WorldLightCalculator.h"
#include "core/organisms/Duck.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/components/LightHandHeld.h"
#include <cmath>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <sstream>

using namespace DirtSim;
using namespace DirtSim::Test;

class DuckLedgeStabilityTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::info); }

    /**
     * Create a world with a raised ledge.
     *
     * Layout (width x height):
     *   Row 0: WALL border (ceiling)
     *   Row 1 to ledge_y-1: AIR
     *   Row ledge_y: WALL ledge from ledge_start_x to ledge_end_x, AIR elsewhere
     *   Row ledge_y+1 to height-2: AIR (gap below ledge)
     *   Row height-1: WALL border (floor)
     *
     * @param width World width.
     * @param height World height.
     * @param ledge_y Y position of ledge surface (duck stands at ledge_y-1).
     * @param ledge_start_x Left edge of ledge.
     * @param ledge_end_x Right edge of ledge (inclusive).
     * @param ledge_height How many cells tall the ledge is.
     */
    std::unique_ptr<World> createLedgeWorld(
        int width,
        int height,
        int ledge_y,
        int ledge_start_x,
        int ledge_end_x,
        int ledge_height = 3)
    {
        auto world = std::make_unique<World>(width, height);

        // Clear interior to air.
        for (int y = 1; y < height - 1; ++y) {
            for (int x = 1; x < width - 1; ++x) {
                world->getData().at(x, y).replaceMaterial(Material::EnumType::Air, 0.0);
            }
        }

        // Build ledge (multiple cells tall).
        for (int h = 0; h < ledge_height; ++h) {
            int y = ledge_y + h;
            if (y < height - 1) {
                for (int x = ledge_start_x; x <= ledge_end_x; ++x) {
                    world->getData().at(x, y).replaceMaterial(Material::EnumType::Wall, 1.0);
                }
            }
        }

        return world;
    }
};

/**
 * Test: Measure the light's equilibrium pitch when standing still.
 *
 * This test runs long enough to find the natural equilibrium where
 * gravity torque balances the duck's corrective torque, then verifies
 * stability around that point.
 *
 * Layout: Duck stands on a cliff edge in the bottom-left, shining into open air.
 * This allows us to measure where the beam actually points without wall absorption.
 *
 *     01234567890123456789
 *  8: ....................
 *  9: ....................
 * 10: ....................
 * 11: ......D.............  <- duck on cliff edge at (6, 11)
 * 12: WWWWWWW.............  <- cliff from x=0 to x=6
 * 13: WWWWWWW.............
 * 14: WWWWWWWWWWWWWWWWWWWW  <- floor (world boundary)
 */
TEST_F(DuckLedgeStabilityTest, LightEquilibriumOnLedge)
{
    constexpr int WIDTH = 20;
    constexpr int HEIGHT = 15;
    constexpr int CLIFF_Y = 12;         // Top surface of cliff.
    constexpr int CLIFF_START_X = 1;    // Left edge (inside wall border).
    constexpr int CLIFF_END_X = 6;      // Right edge - duck stands here.
    constexpr int CLIFF_HEIGHT = 2;     // 2 cells tall.
    constexpr int DUCK_X = 6;           // On the cliff edge.
    constexpr int DUCK_Y = CLIFF_Y - 1; // Standing on cliff (y=11).

    auto world = createLedgeWorld(WIDTH, HEIGHT, CLIFF_Y, CLIFF_START_X, CLIFF_END_X, CLIFF_HEIGHT);

    // Print layout for debugging.
    printWorld(*world, "Cliff-edge layout for flashlight test");

    OrganismManager& manager = world->getOrganismManager();
    auto test_brain = std::make_unique<TestDuckBrain>();
    test_brain->setAction(DuckAction::WAIT);

    OrganismId duck_id = manager.createDuck(*world, DUCK_X, DUCK_Y, std::move(test_brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Create and attach handheld light.
    LightManager& lights = world->getLightManager();
    SpotLight spot{
        .position = Vector2f{ static_cast<float>(DUCK_X), static_cast<float>(DUCK_Y) },
        .color = 0xFFFF00FF,
        .intensity = 1.0f,
        .radius = 12.0f,
        .attenuation = 0.1f,
        .direction = 0.0f,
        .arc_width = 0.8f,
        .focus = 0.5f,
    };
    LightHandle handle = lights.createLight(spot);
    auto handheld = std::make_unique<LightHandHeld>(std::move(handle));
    duck->setHandheldLight(std::move(handheld));

    LightHandHeld* light = duck->getHandheldLight();
    ASSERT_NE(light, nullptr);

    constexpr double DT = 0.016;

    spdlog::info("");
    spdlog::info("=== Light Equilibrium Test (Cliff Edge) ===");
    spdlog::info("Duck at ({}, {}), shining into open air", DUCK_X, DUCK_Y);

    // Log the physics config.
    const auto& cfg = light->getConfig();
    spdlog::info(
        "LightHandHeld config: weight={:.2f}, inertia={:.2f}, max_torque={:.2f}, damping={:.2f}",
        cfg.weight,
        cfg.inertia,
        cfg.max_torque,
        cfg.damping);

    // Run until equilibrium (angular velocity near zero).
    constexpr int MAX_FRAMES = 600; // 10 seconds.
    int equilibrium_frame = -1;
    float equilibrium_pitch = 0.0f;

    for (int frame = 0; frame < MAX_FRAMES; ++frame) {
        world->advanceTime(DT);

        float omega = light->getAngularVelocity();
        float pitch = light->getPitch();

        // Log more frequently at start to see physics settling, then periodically.
        bool should_log = (frame < 30 && frame % 5 == 0) || (frame % 60 == 0);
        if (should_log) {
            // Get spotlight state for position info.
            SpotLight* spot = world->getLightManager().getLight<SpotLight>(light->getLightId());
            float spot_dir = spot ? spot->direction : 0.0f;
            float spot_y = spot ? spot->position.y : 0.0f;

            spdlog::info(
                "Frame {:4d}: pitch={:+.4f}rad ({:+6.2f}°) ω={:+.5f} spot_dir={:+.4f}rad "
                "spot_y={:.2f}",
                frame,
                pitch,
                pitch * 180.0 / M_PI,
                omega,
                spot_dir,
                spot_y);
        }

        // Detect equilibrium: angular velocity very small.
        if (equilibrium_frame < 0 && std::abs(omega) < 0.001f && frame > 120) {
            equilibrium_frame = frame;
            equilibrium_pitch = pitch;
            spdlog::info(
                "Equilibrium reached at frame {}: pitch={:.3f}rad ({:.1f}°)",
                frame,
                pitch,
                pitch * 180.0 / M_PI);
        }
    }

    // Continue for 120 more frames to verify stability.
    spdlog::info("");
    spdlog::info("--- Verifying stability around equilibrium ---");

    float pitch_sum = 0.0f;
    float pitch_min = light->getPitch();
    float pitch_max = light->getPitch();

    for (int frame = 0; frame < 120; ++frame) {
        world->advanceTime(DT);
        float pitch = light->getPitch();
        pitch_sum += pitch;
        pitch_min = std::min(pitch_min, pitch);
        pitch_max = std::max(pitch_max, pitch);
    }

    float pitch_avg = pitch_sum / 120.0f;
    float pitch_range = pitch_max - pitch_min;

    spdlog::info("Average pitch: {:.3f} rad ({:.1f}°)", pitch_avg, pitch_avg * 180.0 / M_PI);
    spdlog::info("Pitch range:   {:.4f} rad ({:.2f}°)", pitch_range, pitch_range * 180.0 / M_PI);

    // Light should have found equilibrium.
    EXPECT_GE(equilibrium_frame, 0) << "Light should reach equilibrium";

    // Equilibrium pitch should be near horizontal when standing still.
    constexpr float MAX_DROOP = 0.05f; // ~3 degrees tolerance.
    EXPECT_LT(std::abs(equilibrium_pitch), MAX_DROOP)
        << "Duck standing still should hold flashlight horizontal, but pitch is "
        << equilibrium_pitch << " rad (" << (equilibrium_pitch * 180.0 / M_PI) << "°)";

    // Pitch should be stable (small range).
    constexpr float MAX_PITCH_RANGE = 0.02f; // ~1 degree oscillation is acceptable.
    EXPECT_LT(pitch_range, MAX_PITCH_RANGE)
        << "Pitch should be stable at equilibrium, but range was " << pitch_range << " rad ("
        << (pitch_range * 180.0 / M_PI) << "°)";

    // Lightmap verification: confirm flashlight is pointing horizontally.
    LightConfig light_config = getDefaultLightConfig();
    light_config.ambient_color = ColorNames::black();
    light_config.ambient_intensity = 0.0f;
    light_config.sun_enabled = false;

    WorldLightCalculator calc;
    Timers timers;
    calc.calculate(*world, world->getGrid(), light_config, timers);

    // Print combined WORLD + LIGHTMAP side by side for visual debugging.
    spdlog::info("");
    spdlog::info("=== COMBINED VIEW: WORLD (left) | LIGHTMAP (right) ===");
    spdlog::info("Duck marked as 'D' in both views. Shades: ' '=dark, '@'=bright");
    spdlog::info("");

    // Build world string (same as printWorld but into a vector of strings).
    std::vector<std::string> world_lines;
    const WorldData& wdata = world->getData();
    for (int y = 0; y < wdata.height; ++y) {
        std::string row_str;
        for (int x = 0; x < wdata.width; ++x) {
            // Mark duck position.
            if (x == DUCK_X && y == DUCK_Y) {
                row_str += 'D';
            }
            else {
                const Cell& cell = wdata.at(x, y);
                if (cell.material_type == Material::EnumType::Wall) {
                    row_str += 'W';
                }
                else if (cell.material_type == Material::EnumType::Air || cell.isEmpty()) {
                    row_str += '.';
                }
                else {
                    row_str += '?';
                }
            }
        }
        world_lines.push_back(row_str);
    }

    // Build lightmap string with duck marked.
    std::string lightmap = calc.lightMapString(*world);
    std::vector<std::string> light_lines;
    std::istringstream iss(lightmap);
    std::string line;
    int row_idx = 0;
    while (std::getline(iss, line)) {
        // Mark duck position in lightmap.
        if (row_idx == DUCK_Y && DUCK_X < static_cast<int>(line.size())) {
            line[DUCK_X] = 'D';
        }
        light_lines.push_back(line);
        row_idx++;
    }

    // Print header with column numbers.
    spdlog::info("      WORLD                 LIGHTMAP");
    spdlog::info("    01234567890123456789  01234567890123456789");

    // Print side by side.
    for (int y = 0; y < static_cast<int>(world_lines.size()); ++y) {
        std::string world_row = (y < static_cast<int>(world_lines.size())) ? world_lines[y] : "";
        std::string light_row = (y < static_cast<int>(light_lines.size())) ? light_lines[y] : "";
        spdlog::info("{:2d}: {}  {}", y, world_row, light_row);
    }

    // Print spotlight info.
    SpotLight* spotlight = world->getLightManager().getLight<SpotLight>(light->getLightId());
    if (spotlight) {
        spdlog::info("");
        spdlog::info("=== SPOTLIGHT STATE ===");
        spdlog::info("Position: ({:.1f}, {:.1f})", spotlight->position.x, spotlight->position.y);
        spdlog::info(
            "Direction: {:.3f} rad ({:.1f} deg)",
            spotlight->direction,
            spotlight->direction * 180.0 / M_PI);
        spdlog::info(
            "Arc width: {:.2f} rad ({:.1f} deg)",
            spotlight->arc_width,
            spotlight->arc_width * 180.0 / M_PI);
        spdlog::info("Intensity: {:.2f}, Radius: {:.1f}", spotlight->intensity, spotlight->radius);
    }

    // Measure brightness in front of duck - all in open AIR now!
    // Duck at x=6, measure at x=12 (6 cells ahead).
    constexpr int MEASURE_X = 12;
    constexpr int MEASURE_DISTANCE = MEASURE_X - DUCK_X;
    WorldData& data = world->getData();

    // Sample 5 rows centered on duck's Y position to capture the beam spread.
    float b_2above = ColorNames::brightness(data.colors.at(MEASURE_X, DUCK_Y - 2));
    float b_1above = ColorNames::brightness(data.colors.at(MEASURE_X, DUCK_Y - 1));
    float b_at = ColorNames::brightness(data.colors.at(MEASURE_X, DUCK_Y));
    float b_1below = ColorNames::brightness(data.colors.at(MEASURE_X, DUCK_Y + 1));
    float b_2below = ColorNames::brightness(data.colors.at(MEASURE_X, DUCK_Y + 2));

    spdlog::info("");
    spdlog::info("=== LIGHTMAP MEASUREMENT at x={} (all AIR) ===", MEASURE_X);
    spdlog::info("  y={}: {:.4f}", DUCK_Y - 2, b_2above);
    spdlog::info("  y={}: {:.4f}", DUCK_Y - 1, b_1above);
    spdlog::info("  y={}: {:.4f} <- duck level", DUCK_Y, b_at);
    spdlog::info("  y={}: {:.4f}", DUCK_Y + 1, b_1below);
    spdlog::info("  y={}: {:.4f}", DUCK_Y + 2, b_2below);

    // Calculate Y centroid of light using weighted average.
    float total = b_2above + b_1above + b_at + b_1below + b_2below;
    float y_centroid = (total > 0.001f)
        ? (b_2above * (DUCK_Y - 2) + b_1above * (DUCK_Y - 1) + b_at * DUCK_Y
           + b_1below * (DUCK_Y + 1) + b_2below * (DUCK_Y + 2))
            / total
        : DUCK_Y;
    float y_offset = y_centroid - DUCK_Y;
    float angle_deg = std::atan(y_offset / MEASURE_DISTANCE) * 180.0f / static_cast<float>(M_PI);

    spdlog::info(
        "Light centroid: y={:.3f}, offset={:.3f} cells, beam angle={:.2f}°",
        y_centroid,
        y_offset,
        angle_deg);

    // Compare beam angle to reported flashlight direction.
    if (spotlight) {
        float reported_angle_deg = spotlight->direction * 180.0f / static_cast<float>(M_PI);
        spdlog::info(
            "Spotlight reports direction={:.2f}°, beam measures at {:.2f}°",
            reported_angle_deg,
            angle_deg);

        // The measured beam angle should match the spotlight's reported direction.
        // Allow 2 degrees tolerance for light spread and measurement granularity.
        float angle_diff = std::abs(angle_deg - reported_angle_deg);
        EXPECT_LT(angle_diff, 2.0f)
            << "Beam direction (" << angle_deg << "°) should match spotlight direction ("
            << reported_angle_deg << "°)";
    }

    // Flashlight should be near horizontal (equilibrium pitch is small).
    EXPECT_LT(std::abs(angle_deg), 5.0f)
        << "Flashlight should point near-horizontally, but beam angle is " << angle_deg << "°";
}

/**
 * Test: Flashlight pitch changes when duck jumps.
 *
 * This tests the integration between Duck and LightHandHeld - verifying that
 * the duck's acceleration during a jump actually reaches the flashlight physics.
 */
TEST_F(DuckLedgeStabilityTest, FlashlightRespondsToDuckJump)
{
    constexpr int WIDTH = 20;
    constexpr int HEIGHT = 15;
    constexpr int LEDGE_Y = 10;

    auto world = createLedgeWorld(WIDTH, HEIGHT, LEDGE_Y, 5, 15, 3);

    OrganismManager& manager = world->getOrganismManager();
    auto test_brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brain_ptr = test_brain.get();
    test_brain->setAction(DuckAction::WAIT);

    OrganismId duck_id = manager.createDuck(*world, 10, LEDGE_Y - 1, std::move(test_brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Attach flashlight.
    LightManager& lights = world->getLightManager();
    SpotLight spot{
        .position = Vector2f{ 10.0f, 9.0f },
        .color = 0xFFFF00FF,
        .intensity = 1.0f,
        .radius = 15.0f,
        .attenuation = 0.1f,
        .direction = 0.0f,
        .arc_width = 0.8f,
        .focus = 0.5f,
    };
    LightHandle handle = lights.createLight(spot);
    duck->setHandheldLight(std::make_unique<LightHandHeld>(std::move(handle)));

    LightHandHeld* light = duck->getHandheldLight();
    ASSERT_NE(light, nullptr);

    constexpr double DT = 0.016;

    spdlog::info("");
    spdlog::info("=== Flashlight Response to Jump Test ===");

    // Phase 1: Let flashlight settle to equilibrium.
    spdlog::info("--- Settling phase ---");
    for (int i = 0; i < 120; ++i) {
        world->advanceTime(DT);
        if (i % 20 == 0) {
            spdlog::info(
                "Settle frame {:3d}: pitch={:+.3f} rad ({:+.1f}°)",
                i,
                light->getPitch(),
                light->getPitch() * 180.0 / M_PI);
        }
    }
    float settled_pitch = light->getPitch();
    spdlog::info(
        "Settled pitch before jump: {:.3f} rad ({:.1f}°)",
        settled_pitch,
        settled_pitch * 180.0 / M_PI);

    // Phase 2: Make duck jump and track pitch changes.
    brain_ptr->setAction(DuckAction::JUMP);

    float max_pitch = settled_pitch;
    float min_pitch = settled_pitch;

    for (int i = 0; i < 60; ++i) {
        world->advanceTime(DT);

        float pitch = light->getPitch();
        max_pitch = std::max(max_pitch, pitch);
        min_pitch = std::min(min_pitch, pitch);

        // Log every 10 frames.
        if (i % 10 == 0) {
            spdlog::info(
                "Frame {:3d}: pitch={:+.3f} rad ({:+.1f}°) ω={:+.3f}",
                i,
                pitch,
                pitch * 180.0 / M_PI,
                light->getAngularVelocity());
        }

        // After first frame, stop requesting jump (edge-triggered).
        if (i == 0) {
            brain_ptr->setAction(DuckAction::WAIT);
        }
    }

    float pitch_range = max_pitch - min_pitch;
    spdlog::info(
        "Pitch range during jump: {:.3f} rad ({:.1f}°)", pitch_range, pitch_range * 180.0 / M_PI);
    spdlog::info("Min pitch: {:.3f} rad, Max pitch: {:.3f} rad", min_pitch, max_pitch);

    // The flashlight pitch should have changed significantly during the jump.
    // A jump creates large acceleration, which should move the pitch.
    constexpr float MIN_EXPECTED_RANGE = 0.1f; // At least ~6 degrees of movement.
    EXPECT_GT(pitch_range, MIN_EXPECTED_RANGE)
        << "Flashlight pitch should change during jump, but range was only " << pitch_range
        << " rad (" << (pitch_range * 180.0 / M_PI) << "°). "
        << "This suggests acceleration isn't reaching the flashlight physics.";
}

/**
 * Test: Light position should have sub-cell precision as duck moves.
 *
 * This tests that the light position changes smoothly within a cell,
 * not just when the duck moves to a new cell.
 */
TEST_F(DuckLedgeStabilityTest, LightPositionSubCellPrecision)
{
    constexpr int WIDTH = 30;
    constexpr int HEIGHT = 10;
    constexpr int FLOOR_Y = 8;

    // Create flat floor world.
    auto world = std::make_unique<World>(WIDTH, HEIGHT);
    for (int y = 1; y < HEIGHT - 1; ++y) {
        for (int x = 1; x < WIDTH - 1; ++x) {
            world->getData().at(x, y).replaceMaterial(Material::EnumType::Air, 0.0);
        }
    }
    for (int x = 1; x < WIDTH - 1; ++x) {
        world->getData().at(x, FLOOR_Y).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    printWorld(*world, "Flat floor for sub-cell light test");

    OrganismManager& manager = world->getOrganismManager();
    auto test_brain = std::make_unique<TestDuckBrain>();
    test_brain->setAction(DuckAction::RUN_RIGHT);

    constexpr int DUCK_START_X = 5;
    constexpr int DUCK_Y = FLOOR_Y - 1;
    OrganismId duck_id = manager.createDuck(*world, DUCK_START_X, DUCK_Y, std::move(test_brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Attach flashlight.
    LightManager& lights = world->getLightManager();
    SpotLight spot{
        .position = Vector2f{ static_cast<float>(DUCK_START_X), static_cast<float>(DUCK_Y) },
        .color = 0xFFFF00FF,
        .intensity = 1.0f,
        .radius = 10.0f,
        .attenuation = 0.1f,
        .direction = 0.0f,
        .arc_width = 0.8f,
        .focus = 0.5f,
    };
    LightHandle handle = lights.createLight(spot);
    duck->setHandheldLight(std::make_unique<LightHandHeld>(std::move(handle)));

    LightHandHeld* light = duck->getHandheldLight();
    ASSERT_NE(light, nullptr);

    SpotLight* spotlight = lights.getLight<SpotLight>(light->getLightId());
    ASSERT_NE(spotlight, nullptr);

    constexpr double DT = 0.016;

    spdlog::info("");
    spdlog::info("=== Light Sub-Cell Position Test ===");
    spdlog::info("Duck walks right, tracking light position each frame");

    // Track unique light positions to verify sub-cell precision.
    std::vector<float> light_x_positions;
    int last_anchor_x = duck->getAnchorCell().x;
    int cell_changes = 0;

    // Run until duck moves at least 3 cells.
    constexpr int MAX_FRAMES = 300;
    for (int frame = 0; frame < MAX_FRAMES; ++frame) {
        world->advanceTime(DT);

        Vector2i anchor = duck->getAnchorCell();
        float light_x = spotlight->position.x;
        float light_y = spotlight->position.y;

        // Get cell COM for debugging.
        const Cell& cell = world->getData().at(anchor.x, anchor.y);

        // Log every frame to see the sub-cell movement.
        if (frame < 60 || frame % 10 == 0) {
            spdlog::info(
                "Frame {:3d}: anchor=({},{}), com=({:+.3f},{:+.3f}), light=({:.3f},{:.3f})",
                frame,
                anchor.x,
                anchor.y,
                cell.com.x,
                cell.com.y,
                light_x,
                light_y);
        }

        light_x_positions.push_back(light_x);

        // Track cell changes.
        if (anchor.x != last_anchor_x) {
            spdlog::info(
                "*** CELL CHANGE: {} -> {} at frame {} ***", last_anchor_x, anchor.x, frame);
            cell_changes++;
            last_anchor_x = anchor.x;
        }

        // Stop after 3 cell changes.
        if (cell_changes >= 3) {
            break;
        }
    }

    // Count unique x positions (with some tolerance for floating point).
    std::vector<float> unique_positions;
    for (float pos : light_x_positions) {
        bool is_unique = true;
        for (float existing : unique_positions) {
            if (std::abs(pos - existing) < 0.001f) {
                is_unique = false;
                break;
            }
        }
        if (is_unique) {
            unique_positions.push_back(pos);
        }
    }

    spdlog::info("");
    spdlog::info("=== RESULTS ===");
    spdlog::info("Total frames: {}", light_x_positions.size());
    spdlog::info("Cell changes: {}", cell_changes);
    spdlog::info("Unique light x positions: {}", unique_positions.size());

    // If sub-cell precision is working, we should have many unique positions.
    // If it's quantized to cells, we'd only have ~cell_changes+1 positions.
    EXPECT_GT(unique_positions.size(), static_cast<size_t>(cell_changes + 1))
        << "Light position should have sub-cell precision. "
        << "Expected many unique positions, but only got " << unique_positions.size() << " for "
        << cell_changes << " cell changes. "
        << "This suggests the light position is quantized to cell boundaries.";
}
