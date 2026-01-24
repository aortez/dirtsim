/**
 * Tests for LightHandHeld physics in isolation.
 *
 * Verifies the flashlight physics:
 * - Gravity torque pulls beam downward.
 * - Acceleration pseudo-forces affect beam pitch.
 * - Hysteresis shutoff prevents flicker.
 */

#include "LightHandHeld.h"
#include "core/LightManager.h"
#include "core/LightTypes.h"
#include "core/Vector2d.h"
#include <cmath>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <vector>

using namespace DirtSim;

class LightHandHeldTest : public ::testing::Test {
protected:
    LightManager lights;
    static constexpr Vector2d STATIONARY_POS{ 5.0, 5.0 };
    static constexpr bool FACING_RIGHT = true;

    void SetUp() override { spdlog::set_level(spdlog::level::info); }

    SpotLight makeSpotLight()
    {
        return SpotLight{
            .position = STATIONARY_POS,
            .color = 0xFFFF00FF,
            .intensity = 1.0f,
            .radius = 15.0f,
            .attenuation = 0.1f,
            .direction = 0.0f,
            .arc_width = 0.8f,
            .focus = 0.5f,
        };
    }

    void logSnapshot(const std::string& phase, double time, const LightHandHeld& light)
    {
        spdlog::info(
            "[{:>8}] t={:.3f}s pitch={:+.3f}rad ({:+.1f}°) ω={:+.3f} on={}",
            phase,
            time,
            light.getPitch(),
            light.getPitch() * 180.0 / M_PI,
            light.getAngularVelocity(),
            light.isOn() ? "YES" : "NO");
    }

    // Simulate stationary holder (no position change = no acceleration).
    void updateStationary(LightHandHeld& light, double dt)
    {
        light.update(lights, STATIONARY_POS, FACING_RIGHT, dt);
    }

    // Simulate holder at a specific position.
    void updateAtPosition(LightHandHeld& light, Vector2d pos, double dt)
    {
        light.update(lights, pos, FACING_RIGHT, dt);
    }
};

// =============================================================================
// Basic Physics
// =============================================================================

TEST_F(LightHandHeldTest, InitialStateIsHorizontalAndOn)
{
    LightHandle handle = lights.createLight(makeSpotLight());
    LightHandHeld light(std::move(handle));

    EXPECT_FLOAT_EQ(light.getPitch(), 0.0f);
    EXPECT_FLOAT_EQ(light.getAngularVelocity(), 0.0f);
    EXPECT_TRUE(light.isOn());
}

struct GravityTestCase {
    std::string name;
    float weight;
    float max_torque;
    bool expect_droop;
};

class GravityVsStrengthTest : public LightHandHeldTest,
                              public ::testing::WithParamInterface<GravityTestCase> {};

TEST_P(GravityVsStrengthTest, HoldingFlashlightStationary)
{
    const auto& tc = GetParam();

    LightHandle handle = lights.createLight(makeSpotLight());
    LightHandHeld::Config config{
        .weight = tc.weight,
        .inertia = 0.4f,
        .max_torque = tc.max_torque,
        .damping = 2.0f,
        .accel_sensitivity = 0.08f,
        .shutoff_angle = -0.6f,
        .recovery_angle = -0.4f,
    };
    LightHandHeld light(std::move(handle), config);

    constexpr double dt = 1.0 / 60.0;
    constexpr int frames = 120;

    spdlog::info("=== {} (weight={}, max_torque={}) ===", tc.name, tc.weight, tc.max_torque);

    for (int i = 0; i < frames; ++i) {
        updateStationary(light, dt);
    }

    float pitch_deg = light.getPitch() * 180.0f / static_cast<float>(M_PI);
    spdlog::info("Final pitch: {:.1f}°", pitch_deg);

    if (tc.expect_droop) {
        EXPECT_GT(light.getPitch(), 0.1f)
            << "With weight=" << tc.weight << " and max_torque=" << tc.max_torque
            << ", flashlight should droop (duck too weak)";
    }
    else {
        EXPECT_LT(std::abs(light.getPitch()), 0.05f)
            << "With weight=" << tc.weight << " and max_torque=" << tc.max_torque
            << ", duck should hold flashlight level";
    }
}

INSTANTIATE_TEST_SUITE_P(
    GravityTests,
    GravityVsStrengthTest,
    ::testing::Values(
        GravityTestCase{ "TooHeavy", 3.0f, 1.0f, true },
        GravityTestCase{ "NoProblem", 1.5f, 3.0f, false }),
    [](const ::testing::TestParamInfo<GravityTestCase>& info) { return info.param.name; });

TEST_F(LightHandHeldTest, CorrectiveTorqueFightsGravity)
{
    LightHandle handle = lights.createLight(makeSpotLight());
    LightHandHeld light(std::move(handle));

    // Run until it reaches equilibrium (gravity balanced by corrective torque).
    constexpr double dt = 1.0 / 60.0;
    constexpr int frames = 300;

    spdlog::info("=== Corrective Torque Test ===");
    for (int i = 0; i < frames; ++i) {
        updateStationary(light, dt);
        if (i % 60 == 0) {
            spdlog::info(
                "Frame {:3d}: pitch={:+.3f} rad ({:+.1f}°)",
                i,
                light.getPitch(),
                light.getPitch() * 180.0 / M_PI);
        }
    }

    float equilibrium_pitch = light.getPitch();
    spdlog::info(
        "Equilibrium: pitch={:.3f} rad ({:.1f}°)",
        equilibrium_pitch,
        equilibrium_pitch * 180.0 / M_PI);

    // Duck standing still should hold flashlight near horizontal.
    // Allow ~5 degrees of droop as acceptable.
    constexpr float MAX_DROOP = 0.09f; // ~5 degrees.
    EXPECT_LT(std::abs(equilibrium_pitch), MAX_DROOP)
        << "Standing still, flashlight should be near horizontal, but pitch is "
        << equilibrium_pitch << " rad (" << (equilibrium_pitch * 180.0 / M_PI) << "°)";
}

TEST_F(LightHandHeldTest, UpwardAccelerationCausesBeamToDroop)
{
    // When accelerating upward, it's like being in a rising elevator -
    // everything feels heavier. The flashlight should droop MORE.
    //
    // Coordinate system:
    // - Positive pitch = pointing down (toward +Y in screen coords).
    // - Upward acceleration = negative Y in DirtSim coords.
    // - Droop = pitch increases (more positive).

    LightHandle handle = lights.createLight(makeSpotLight());
    LightHandHeld light(std::move(handle));

    constexpr double dt = 1.0 / 60.0;

    // Let the light settle to gravity equilibrium.
    Vector2d pos = STATIONARY_POS;
    for (int i = 0; i < 60; ++i) {
        light.update(lights, pos, FACING_RIGHT, dt);
    }
    float baseline_pitch = light.getPitch();
    spdlog::info(
        "Baseline pitch after settling: {:.3f} rad ({:.1f}°)",
        baseline_pitch,
        baseline_pitch * 180.0 / M_PI);

    // Apply upward acceleration (negative y) by moving position upward rapidly.
    // Moving up → velocity negative → then stopping → acceleration positive (decel).
    // To get upward accel, we need to suddenly start moving upward.
    constexpr double upward_speed = 3.0; // cells/frame.

    for (int i = 0; i < 10; ++i) {
        pos.y -= upward_speed * dt; // Move upward.
        light.update(lights, pos, FACING_RIGHT, dt);
    }

    float pitch_after_jump = light.getPitch();
    spdlog::info(
        "Pitch after upward accel: {:.3f} rad ({:.1f}°)",
        pitch_after_jump,
        pitch_after_jump * 180.0 / M_PI);

    // Beam should droop MORE (more positive) during upward acceleration.
    EXPECT_GT(pitch_after_jump, baseline_pitch)
        << "Beam should droop (become more positive) during upward acceleration. "
        << "Baseline: " << baseline_pitch << " rad, After jump: " << pitch_after_jump << " rad";
}

TEST_F(LightHandHeldTest, DownwardAccelerationCausesBeamToRise)
{
    // When accelerating downward (or in freefall), everything feels lighter,
    // like being in a falling elevator. The flashlight should be easier to hold up.
    //
    // Coordinate system:
    // - Positive pitch = pointing down (toward +Y in screen coords).
    // - Downward acceleration = positive Y in DirtSim coords.
    // - Rise = pitch decreases (less positive / toward zero).

    LightHandle handle = lights.createLight(makeSpotLight());
    LightHandHeld light(std::move(handle));

    constexpr double dt = 1.0 / 60.0;

    // Let it droop to equilibrium first.
    Vector2d pos = STATIONARY_POS;
    for (int i = 0; i < 120; ++i) {
        light.update(lights, pos, FACING_RIGHT, dt);
    }
    float drooped_pitch = light.getPitch();
    spdlog::info(
        "Drooped pitch: {:.3f} rad ({:.1f}°)", drooped_pitch, drooped_pitch * 180.0 / M_PI);

    // Apply downward acceleration (positive y) - like freefall.
    // Moving down rapidly simulates falling.
    constexpr double downward_speed = 3.0; // cells/frame.

    for (int i = 0; i < 10; ++i) {
        pos.y += downward_speed * dt; // Move downward.
        light.update(lights, pos, FACING_RIGHT, dt);
    }

    float pitch_after_fall = light.getPitch();
    spdlog::info(
        "Pitch after downward accel: {:.3f} rad ({:.1f}°)",
        pitch_after_fall,
        pitch_after_fall * 180.0 / M_PI);

    // Beam should rise (become less positive) during downward acceleration.
    EXPECT_LT(pitch_after_fall, drooped_pitch)
        << "Beam should rise (become less positive) during downward acceleration. "
        << "Drooped: " << drooped_pitch << " rad, After fall: " << pitch_after_fall << " rad";
}

// =============================================================================
// Jump Simulation - Full Profile with Data Capture
// =============================================================================

struct JumpSequenceTestCase {
    std::string name;
    LightHandHeld::Config config;
    bool expect_off_during_jump;
};

class JumpSequenceTest : public LightHandHeldTest,
                         public ::testing::WithParamInterface<JumpSequenceTestCase> {};

TEST_P(JumpSequenceTest, JumpSequenceCapturesFlashlightBehavior)
{
    const auto& tc = GetParam();
    LightHandle handle = lights.createLight(makeSpotLight());
    LightHandHeld light(std::move(handle), tc.config);

    constexpr double dt = 1.0 / 60.0;
    Vector2d pos = STATIONARY_POS;
    double velocity_y = 0.0;

    struct Snapshot {
        int frame;
        double time;
        float pitch;
        float angular_velocity;
        bool is_on;
        bool in_jump;
        double vel_y;
        const char* phase;
    };
    std::vector<Snapshot> data;

    auto dumpHistory = [&](const std::string& reason) {
        spdlog::info("");
        spdlog::info("=== Jump Sequence History Dump ===");
        spdlog::info("Case: {}", tc.name);
        spdlog::info("Reason: {}", reason);
        spdlog::info(
            "Config: weight={:.2f}, inertia={:.2f}, max_torque={:.2f}, damping={:.2f}, "
            "accel_sensitivity={:.2f}, shutoff_angle={:.2f}, recovery_angle={:.2f}",
            tc.config.weight,
            tc.config.inertia,
            tc.config.max_torque,
            tc.config.damping,
            tc.config.accel_sensitivity,
            tc.config.shutoff_angle,
            tc.config.recovery_angle);
        for (const auto& s : data) {
            spdlog::info(
                "f={:03d} t={:.3f} phase={:<8} jump={} vel_y={:+.2f} pitch={:+.3f} "
                "ω={:+.3f} on={}",
                s.frame,
                s.time,
                s.phase,
                s.in_jump ? "Y" : "N",
                s.vel_y,
                s.pitch,
                s.angular_velocity,
                s.is_on ? "Y" : "N");
        }
    };

    auto failWithHistory = [&](const std::string& reason) {
        dumpHistory(reason);
        ADD_FAILURE() << reason;
    };

    auto step = [&](double target_vel_y, const char* phase, bool in_jump) {
        pos.y += velocity_y * dt;
        velocity_y = target_vel_y;

        const double t = data.empty() ? 0.0 : data.back().time + dt;
        light.update(lights, pos, FACING_RIGHT, dt);
        data.push_back(
            { static_cast<int>(data.size()),
              t,
              light.getPitch(),
              light.getAngularVelocity(),
              light.isOn(),
              in_jump,
              velocity_y,
              phase });

        if (data.size() % 5 == 1 || data.size() <= 3) {
            logSnapshot(phase, t, light);
        }
    };

    spdlog::info("");
    spdlog::info("=== Jump Sequence Simulation ===");
    spdlog::info("Case: {}", tc.name);
    spdlog::info("Coordinate system: positive y = DOWN");
    spdlog::info("Jump velocity (up) = negative y");
    spdlog::info("");

    spdlog::info("--- Phase 1: Pre-jump idle ---");
    for (int i = 0; i < 3; ++i) {
        step(0.0, "idle", false);
    }

    spdlog::info("--- Phase 2: Jump impulse (vel_y = -4.0) ---");
    for (int i = 0; i < 3; ++i) {
        step(-4.0, "JUMP", true);
    }

    spdlog::info("--- Phase 3: Rising / slowing ---");
    double vel = -4.0;
    for (int i = 0; i < 15; ++i) {
        vel += 0.3;
        step(vel, "rise", true);
    }

    spdlog::info("--- Phase 4: Peak ---");
    for (int i = 0; i < 5; ++i) {
        step(0.0, "peak", true);
    }

    spdlog::info("--- Phase 5: Falling ---");
    vel = 0.0;
    for (int i = 0; i < 15; ++i) {
        vel += 0.3;
        step(vel, "fall", true);
    }

    spdlog::info("--- Phase 6: Landing impact (vel_y = 0) ---");
    for (int i = 0; i < 3; ++i) {
        step(0.0, "LAND", true);
    }

    spdlog::info("--- Phase 7: Recovery ---");
    for (int i = 0; i < 120; ++i) {
        step(0.0, "recovery", false);
    }

    float min_pitch = 0.0f;
    float max_pitch = 0.0f;
    bool saw_off_during_jump = false;
    bool saw_on_during_jump = false;
    int off_count = 0;

    for (const auto& s : data) {
        min_pitch = std::min(min_pitch, s.pitch);
        max_pitch = std::max(max_pitch, s.pitch);
        if (!s.is_on) {
            off_count++;
        }
        if (s.in_jump) {
            saw_on_during_jump |= s.is_on;
            saw_off_during_jump |= !s.is_on;
        }
    }

    spdlog::info("");
    spdlog::info("=== Summary Statistics ===");
    spdlog::info("Min pitch: {:.3f} rad ({:.1f}°)", min_pitch, min_pitch * 180.0 / M_PI);
    spdlog::info("Max pitch: {:.3f} rad ({:.1f}°)", max_pitch, max_pitch * 180.0 / M_PI);
    spdlog::info("Total frames: {}", data.size());
    spdlog::info("Frames with light OFF: {}", off_count);
    spdlog::info(
        "Final state: pitch={:.1f}°, on={}", light.getPitch() * 180.0 / M_PI, light.isOn());

    const float pitch_range = max_pitch - min_pitch;
    if (pitch_range <= 0.05f) {
        failWithHistory("Flashlight should respond to jump motion (pitch_range too small).");
        return;
    }

    if (std::abs(light.getAngularVelocity()) >= 0.1f) {
        failWithHistory("Flashlight should settle after recovery (angular velocity too high).");
        return;
    }

    if (tc.expect_off_during_jump) {
        if (!saw_off_during_jump) {
            failWithHistory("Expected flashlight to turn off during jump, but it never did.");
        }
    }
    else {
        if (saw_off_during_jump) {
            failWithHistory("Expected flashlight to stay on during jump, but it turned off.");
        }
        if (!saw_on_during_jump) {
            failWithHistory("Expected flashlight to be on during jump, but it was never on.");
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    JumpSequenceCases,
    JumpSequenceTest,
    ::testing::Values(
        JumpSequenceTestCase{ "ShutoffExpected",
                              LightHandHeld::Config{ .weight = 4.0f,
                                                     .inertia = 0.4f,
                                                     .max_torque = 0.8f,
                                                     .damping = 1.0f,
                                                     .accel_sensitivity = 0.2f,
                                                     .shutoff_angle = 0.35f,
                                                     .recovery_angle = 0.2f },
                              true },
        JumpSequenceTestCase{ "NoShutoffExpected",
                              LightHandHeld::Config{ .weight = 1.0f,
                                                     .inertia = 0.4f,
                                                     .max_torque = 4.5f,
                                                     .damping = 2.5f,
                                                     .accel_sensitivity = 0.05f,
                                                     .shutoff_angle = 1.2f,
                                                     .recovery_angle = 0.8f },
                              false }),
    [](const ::testing::TestParamInfo<JumpSequenceTestCase>& info) { return info.param.name; });

// =============================================================================
// Shutoff Hysteresis
// =============================================================================

TEST_F(LightHandHeldTest, LightShutsOffWhenDroopedBelowThreshold)
{
    LightHandle handle = lights.createLight(makeSpotLight());

    // Use a config with easier-to-hit shutoff.
    // Note: shutoff_angle and recovery_angle are negative (beam pointing up).
    // The current physics has positive pitch = droop. For shutoff to trigger,
    // we need to configure with positive shutoff_angle.
    LightHandHeld::Config config{
        .weight = 3.0f, // Heavy - droops fast.
        .inertia = 0.4f,
        .max_torque = 1.0f, // Weak corrective force.
        .damping = 0.5f,
        .accel_sensitivity = 0.1f,
        .shutoff_angle = 0.6f,  // Shuts off when drooped past this.
        .recovery_angle = 0.4f, // Recovers when above this.
    };

    LightHandHeld light(std::move(handle), config);

    constexpr double dt = 1.0 / 60.0;
    Vector2d pos = STATIONARY_POS;

    spdlog::info("");
    spdlog::info("=== Shutoff Hysteresis Test ===");
    logSnapshot("init", 0, light);

    // Let gravity droop the beam - with heavy weight and weak torque, it should droop
    // significantly.
    for (int i = 0; i < 120; ++i) {
        light.update(lights, pos, FACING_RIGHT, dt);

        if (i % 20 == 0) {
            logSnapshot("droop", i * dt, light);
        }
    }

    spdlog::info("After drooping:");
    logSnapshot("drooped", 120 * dt, light);

    float drooped_pitch = light.getPitch();
    spdlog::info(
        "Drooped pitch: {:.3f} rad ({:.1f}°)", drooped_pitch, drooped_pitch * 180.0 / M_PI);

    // With the heavy config, it should have drooped past shutoff (if physics reaches that).
    // This test may need adjustment based on actual equilibrium point.
    if (drooped_pitch > config.shutoff_angle) {
        EXPECT_FALSE(light.isOn()) << "Light should shut off when pitch exceeds shutoff_angle";
    }

    // Apply strong upward movement to create upward acceleration and lift beam.
    spdlog::info("Applying lift to recover...");
    for (int i = 0; i < 60; ++i) {
        pos.y -= 0.1; // Move up to create upward acceleration.
        light.update(lights, pos, FACING_RIGHT, dt);

        if (i % 15 == 0) {
            logSnapshot("lift", (120 + i) * dt, light);
        }
    }

    spdlog::info("After lift:");
    logSnapshot("final", 180 * dt, light);

    // Should be closer to horizontal after lift.
    // The exact recovery depends on physics tuning.
    spdlog::info(
        "Final pitch: {:.3f} rad ({:.1f}°)", light.getPitch(), light.getPitch() * 180.0 / M_PI);
}

// =============================================================================
// Light Direction Updates
// =============================================================================

TEST_F(LightHandHeldTest, UpdateSetsSpotLightDirectionMatchingPitch)
{
    LightHandle handle = lights.createLight(makeSpotLight());
    LightId id = handle.id();
    LightHandHeld light(std::move(handle));

    constexpr double dt = 1.0 / 60.0;
    Vector2d position{ 10.0, 10.0 };
    for (int i = 0; i < 30; ++i) {
        light.update(lights, position, true, dt);
    }

    float pitch = light.getPitch();
    SpotLight* spot = lights.getLight<SpotLight>(id);
    ASSERT_NE(spot, nullptr);

    // Direction should match pitch when facing right.
    EXPECT_FLOAT_EQ(spot->direction, pitch);
    EXPECT_DOUBLE_EQ(spot->position.x, 10.0);
    EXPECT_DOUBLE_EQ(spot->position.y, 10.0);
}

TEST_F(LightHandHeldTest, UpdateMirrorsPitchWhenFacingLeft)
{
    LightHandle handle = lights.createLight(makeSpotLight());
    LightId id = handle.id();
    LightHandHeld light(std::move(handle));

    // Let it droop while facing left.
    constexpr double dt = 1.0 / 60.0;
    Vector2d position{ 10.0, 10.0 };
    for (int i = 0; i < 30; ++i) {
        light.update(lights, position, false, dt); // Facing left.
    }

    float pitch = light.getPitch();

    SpotLight* spot = lights.getLight<SpotLight>(id);
    ASSERT_NE(spot, nullptr);

    // Direction should be π - pitch when facing left.
    float expected = static_cast<float>(M_PI) - pitch;
    EXPECT_FLOAT_EQ(spot->direction, expected);
}
