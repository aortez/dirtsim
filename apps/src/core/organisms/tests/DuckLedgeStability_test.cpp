/**
 * @file DuckLedgeStability_test.cpp
 * @brief Integration tests for duck standing on a ledge while holding a light.
 *
 * These tests expose physics stability issues when the duck should be at rest.
 * When standing still on a ledge, the duck's velocity should be near-zero and
 * the handheld light should reach a stable equilibrium - not wobble from
 * spurious acceleration.
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
#include <vector>

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

    /**
     * Data snapshot for analysis.
     */
    struct Snapshot {
        int frame;
        double time;
        Vector2i position;
        Vector2d velocity;
        Vector2d acceleration;
        float light_pitch;
        float light_angular_velocity;
        bool light_on;
        bool on_ground;
    };

    void logSnapshot(const Snapshot& s)
    {
        spdlog::info(
            "Frame {:4d} t={:.3f}s pos=({:2d},{:2d}) vel=({:+6.3f},{:+6.3f}) "
            "acc=({:+7.1f},{:+7.1f}) pitch={:+.3f}rad ({:+5.1f}°) ω={:+.3f} on={} gnd={}",
            s.frame,
            s.time,
            s.position.x,
            s.position.y,
            s.velocity.x,
            s.velocity.y,
            s.acceleration.x,
            s.acceleration.y,
            s.light_pitch,
            s.light_pitch * 180.0 / M_PI,
            s.light_angular_velocity,
            s.light_on ? "Y" : "N",
            s.on_ground ? "Y" : "N");
    }
};

/**
 * Test: Duck standing still on a ledge with handheld light.
 *
 * Expected: After settling, velocity should be near-zero and light should
 * reach stable equilibrium with minimal oscillation.
 *
 * This test exposes physics instabilities that cause the light to wobble
 * when the duck should be at rest.
 */
TEST_F(DuckLedgeStabilityTest, StandingOnLedgeWithLight)
{
    // Create world with a 3-cell-high ledge.
    // World: 20x15
    // Ledge at y=10 (duck stands at y=9), spanning x=5 to x=15.
    constexpr int WIDTH = 20;
    constexpr int HEIGHT = 15;
    constexpr int LEDGE_Y = 10;
    constexpr int LEDGE_START_X = 5;
    constexpr int LEDGE_END_X = 15;
    constexpr int LEDGE_HEIGHT = 3;

    auto world = createLedgeWorld(WIDTH, HEIGHT, LEDGE_Y, LEDGE_START_X, LEDGE_END_X, LEDGE_HEIGHT);

    // Print initial world.
    printWorld(*world, "Ledge world layout");

    // Create duck on the ledge (one cell above ledge surface).
    int duck_x = 10;
    int duck_y = LEDGE_Y - 1; // Standing on ledge.

    OrganismManager& manager = world->getOrganismManager();
    auto test_brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brain_ptr = test_brain.get();

    OrganismId duck_id = manager.createDuck(*world, duck_x, duck_y, std::move(test_brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Create and attach handheld light.
    LightManager& lights = world->getLightManager();
    SpotLight spot{
        .position = Vector2d{ static_cast<double>(duck_x), static_cast<double>(duck_y) },
        .color = 0xFFFF00FF,
        .intensity = 1.0f,
        .radius = 15.0f,
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

    // Tell duck to wait (do nothing).
    brain_ptr->setAction(DuckAction::WAIT);

    // Data collection.
    std::vector<Snapshot> data;
    constexpr double DT = 0.016; // 60 FPS.
    Vector2d prev_velocity{ 0.0, 0.0 };

    spdlog::info("");
    spdlog::info("=== Duck on Ledge Stability Test ===");
    spdlog::info("Duck starts at ({}, {}), ledge at y={}", duck_x, duck_y, LEDGE_Y);
    spdlog::info("");

    // Phase 1: Settling (let physics stabilize).
    spdlog::info("--- Phase 1: Settling (60 frames) ---");
    for (int frame = 0; frame < 60; ++frame) {
        world->advanceTime(DT);

        Vector2i pos = duck->getAnchorCell();
        const Cell& cell = world->getData().at(pos.x, pos.y);

        Vector2d acceleration{ 0.0, 0.0 };
        acceleration.x = (cell.velocity.x - prev_velocity.x) / DT;
        acceleration.y = (cell.velocity.y - prev_velocity.y) / DT;

        Snapshot s{
            .frame = frame,
            .time = frame * DT,
            .position = pos,
            .velocity = cell.velocity,
            .acceleration = acceleration,
            .light_pitch = light->getPitch(),
            .light_angular_velocity = light->getAngularVelocity(),
            .light_on = light->isOn(),
            .on_ground = duck->isOnGround(),
        };
        data.push_back(s);

        // Log every 10 frames.
        if (frame % 10 == 0) {
            logSnapshot(s);
        }

        prev_velocity = cell.velocity;
    }

    // Phase 2: Steady state observation (measure stability).
    spdlog::info("");
    spdlog::info("--- Phase 2: Steady State Observation (120 frames) ---");

    // Track statistics for the steady state phase.
    double max_vel_magnitude = 0.0;
    double max_accel_magnitude = 0.0;
    double max_pitch_deviation = 0.0;
    float pitch_at_start = light->getPitch();
    int frames_light_off = 0;

    for (int frame = 60; frame < 180; ++frame) {
        world->advanceTime(DT);

        Vector2i pos = duck->getAnchorCell();
        const Cell& cell = world->getData().at(pos.x, pos.y);

        Vector2d acceleration{ 0.0, 0.0 };
        acceleration.x = (cell.velocity.x - prev_velocity.x) / DT;
        acceleration.y = (cell.velocity.y - prev_velocity.y) / DT;

        Snapshot s{
            .frame = frame,
            .time = frame * DT,
            .position = pos,
            .velocity = cell.velocity,
            .acceleration = acceleration,
            .light_pitch = light->getPitch(),
            .light_angular_velocity = light->getAngularVelocity(),
            .light_on = light->isOn(),
            .on_ground = duck->isOnGround(),
        };
        data.push_back(s);

        // Track max values.
        double vel_mag =
            std::sqrt(cell.velocity.x * cell.velocity.x + cell.velocity.y * cell.velocity.y);
        double acc_mag =
            std::sqrt(acceleration.x * acceleration.x + acceleration.y * acceleration.y);
        max_vel_magnitude = std::max(max_vel_magnitude, vel_mag);
        max_accel_magnitude = std::max(max_accel_magnitude, acc_mag);

        float pitch_dev = std::abs(light->getPitch() - pitch_at_start);
        max_pitch_deviation = std::max(max_pitch_deviation, static_cast<double>(pitch_dev));

        if (!light->isOn()) {
            frames_light_off++;
        }

        // Log every 20 frames.
        if (frame % 20 == 0) {
            logSnapshot(s);
        }

        prev_velocity = cell.velocity;
    }

    // Summary.
    spdlog::info("");
    spdlog::info("=== Stability Summary (frames 60-180) ===");
    spdlog::info("Max velocity magnitude:     {:.6f} cells/s", max_vel_magnitude);
    spdlog::info("Max acceleration magnitude: {:.1f} cells/s²", max_accel_magnitude);
    spdlog::info(
        "Pitch at start of steady:   {:.3f} rad ({:.1f}°)",
        pitch_at_start,
        pitch_at_start * 180.0 / M_PI);
    spdlog::info(
        "Max pitch deviation:        {:.4f} rad ({:.2f}°)",
        max_pitch_deviation,
        max_pitch_deviation * 180.0 / M_PI);
    spdlog::info("Frames light off:           {}", frames_light_off);
    spdlog::info(
        "Final pitch:                {:.3f} rad ({:.1f}°)",
        light->getPitch(),
        light->getPitch() * 180.0 / M_PI);
    spdlog::info("Final angular velocity:     {:.4f} rad/s", light->getAngularVelocity());

    // Assertions - what we EXPECT for a stable duck standing still.
    // These may fail initially, showing us the problems.

    // Duck should remain on ground.
    EXPECT_TRUE(duck->isOnGround()) << "Duck should stay on ground while standing";

    // Duck position should not have changed.
    EXPECT_EQ(duck->getAnchorCell().x, duck_x) << "Duck should not move horizontally";
    EXPECT_EQ(duck->getAnchorCell().y, duck_y) << "Duck should not move vertically";

    // Velocity should be near-zero when standing still.
    // Allowing small tolerance for physics settling.
    constexpr double VELOCITY_TOLERANCE = 0.01;
    EXPECT_LT(max_vel_magnitude, VELOCITY_TOLERANCE)
        << "Duck standing still should have near-zero velocity, but max was " << max_vel_magnitude;

    // Acceleration should be near-zero when standing still.
    // This is the key metric - spurious acceleration causes light wobble.
    constexpr double ACCEL_TOLERANCE = 1.0; // cells/s² - even 1 is pretty noisy.
    EXPECT_LT(max_accel_magnitude, ACCEL_TOLERANCE)
        << "Duck standing still should have near-zero acceleration, but max was "
        << max_accel_magnitude;

    // Light should have stable pitch (small oscillations OK, big ones are not).
    constexpr double PITCH_DEVIATION_TOLERANCE = 0.01; // ~0.5 degrees.
    EXPECT_LT(max_pitch_deviation, PITCH_DEVIATION_TOLERANCE)
        << "Light pitch should be stable, but max deviation was " << max_pitch_deviation << " rad ("
        << (max_pitch_deviation * 180.0 / M_PI) << "°)";

    // Light should stay on (not droop so far it shuts off).
    EXPECT_EQ(frames_light_off, 0) << "Light should stay on when duck is standing still";
}

/**
 * Test: Measure the light's equilibrium pitch when standing still.
 *
 * This test runs long enough to find the natural equilibrium where
 * gravity torque balances the duck's corrective torque, then verifies
 * stability around that point.
 */
TEST_F(DuckLedgeStabilityTest, LightEquilibriumOnLedge)
{
    // Same setup as above.
    constexpr int WIDTH = 20;
    constexpr int HEIGHT = 15;
    constexpr int LEDGE_Y = 10;

    auto world = createLedgeWorld(WIDTH, HEIGHT, LEDGE_Y, 5, 15, 3);

    OrganismManager& manager = world->getOrganismManager();
    auto test_brain = std::make_unique<TestDuckBrain>();
    test_brain->setAction(DuckAction::WAIT);

    OrganismId duck_id = manager.createDuck(*world, 10, LEDGE_Y - 1, std::move(test_brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Create and attach handheld light.
    LightManager& lights = world->getLightManager();
    SpotLight spot{
        .position = Vector2d{ 10.0, 9.0 },
        .color = 0xFFFF00FF,
        .intensity = 1.0f,
        .radius = 15.0f,
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
    spdlog::info("=== Light Equilibrium Test ===");

    // Run until equilibrium (angular velocity near zero).
    constexpr int MAX_FRAMES = 600; // 10 seconds.
    int equilibrium_frame = -1;
    float equilibrium_pitch = 0.0f;

    for (int frame = 0; frame < MAX_FRAMES; ++frame) {
        world->advanceTime(DT);

        float omega = light->getAngularVelocity();
        float pitch = light->getPitch();

        // Log periodically.
        if (frame % 60 == 0) {
            spdlog::info(
                "Frame {:4d}: pitch={:+.3f}rad ({:+5.1f}°) ω={:+.4f}",
                frame,
                pitch,
                pitch * 180.0 / M_PI,
                omega);
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

    // Equilibrium pitch should be negative (gravity pulls down) but not too extreme.
    const auto& config = light->getConfig();
    EXPECT_LT(equilibrium_pitch, 0.0f) << "Equilibrium pitch should be negative (drooping)";
    EXPECT_GT(equilibrium_pitch, config.shutoff_angle)
        << "Equilibrium pitch should be above shutoff threshold";

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

    // Measure brightness in front of duck (x=15, 5 cells ahead).
    int duck_y = LEDGE_Y - 1;
    WorldData& data = world->getData();
    float b_above = ColorNames::brightness(data.colors.at(15, duck_y - 1));
    float b_at = ColorNames::brightness(data.colors.at(15, duck_y));
    float b_below = ColorNames::brightness(data.colors.at(15, duck_y + 1));

    spdlog::info("Lightmap at x=15: above={:.4f}, at={:.4f}, below={:.4f}", b_above, b_at, b_below);

    // Calculate Y centroid of light.
    float total = b_above + b_at + b_below;
    float y_centroid = (total > 0.001f)
        ? (b_above * (duck_y - 1) + b_at * duck_y + b_below * (duck_y + 1)) / total
        : duck_y;
    float y_offset = y_centroid - duck_y;
    float angle_deg = std::atan(y_offset / 5.0f) * 180.0f / static_cast<float>(M_PI);

    spdlog::info("Light centroid offset: {:.3f} cells, angle: {:.2f}°", y_offset, angle_deg);

    // Flashlight should be horizontal within 1 degree.
    EXPECT_LT(std::abs(angle_deg), 1.0f)
        << "Flashlight should point horizontally (within 1°), but angle is " << angle_deg << "°";
}
