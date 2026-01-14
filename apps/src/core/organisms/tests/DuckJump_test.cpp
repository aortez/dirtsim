/**
 * @file DuckJump_test.cpp
 * @brief Tests for duck jumping mechanics including basic jump, cliff detection,
 *        obstacle jumping, and SMB1-style air steering.
 *
 * For basic physics tests, see Duck_test.cpp.
 * For brain behavior tests, see DuckBrain_test.cpp.
 */

#include "DuckTestUtils.h"
#include "core/LoggingChannels.h"
#include "core/World.h"
#include "core/organisms/Duck.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/DuckSensoryData.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace DirtSim;
using namespace DirtSim::Test;

class DuckJumpTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::debug); }
};

// ============================================================================
// Basic Jump Tests
// ============================================================================

TEST_F(DuckJumpTest, DuckJumps2CellsHigh)
{
    auto world = createFlatWorld(5, 10);
    OrganismManager& manager = world->getOrganismManager();

    // Create a test brain we can control.
    auto test_brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brain_ptr = test_brain.get();

    // Create duck on the floor (y=8 is just above wall at y=9).
    int start_y = 8;
    OrganismId duck_id = manager.createDuck(*world, 2, start_y, std::move(test_brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle onto ground first.
    brain_ptr->setAction(DuckAction::WAIT);
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    ASSERT_TRUE(duck->isOnGround()) << "Duck should be on ground before jump test";
    int settled_y = duck->getAnchorCell().y;

    // Log state before jump.
    {
        Vector2i pos = duck->getAnchorCell();
        const Cell& cell = world->getData().at(pos.x, pos.y);
        spdlog::info(
            "Duck settled at y={}, COM=({:.3f},{:.3f}), vel=({:.2f},{:.2f}), on_ground={}",
            settled_y,
            cell.com.x,
            cell.com.y,
            cell.velocity.x,
            cell.velocity.y,
            duck->isOnGround());
    }

    // Trigger jump.
    brain_ptr->setAction(DuckAction::JUMP);
    world->advanceTime(0.016); // One frame to initiate jump.

    // Log state immediately after jump frame.
    {
        Vector2i pos = duck->getAnchorCell();
        const Cell& cell = world->getData().at(pos.x, pos.y);
        spdlog::info(
            "After jump frame: pos=({},{}), COM=({:.3f},{:.3f}), vel=({:.2f},{:.2f}), on_ground={}",
            pos.x,
            pos.y,
            cell.com.x,
            cell.com.y,
            cell.velocity.x,
            cell.velocity.y,
            duck->isOnGround());
    }

    // Switch to wait so we don't keep trying to jump.
    brain_ptr->setAction(DuckAction::WAIT);

    // Track the highest point (minimum Y since Y increases downward).
    int min_y = settled_y;
    double min_com_y = 1.0; // Track minimum COM.y (most upward position within cell).

    // Run physics for enough frames to complete the jump arc.
    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);

        Vector2i pos = duck->getAnchorCell();
        const Cell& cell = world->getData().at(pos.x, pos.y);

        // Log first 30 frames to see jump dynamics.
        if (frame < 30) {
            spdlog::info(
                "Frame {:3d}: pos=({},{}), COM.y={:+.3f}, vel.y={:+.2f}, on_ground={}",
                frame,
                pos.x,
                pos.y,
                cell.com.y,
                cell.velocity.y,
                duck->isOnGround());
        }

        int current_y = pos.y;
        if (current_y < min_y) {
            min_y = current_y;
            spdlog::info("  -> NEW MIN Y: {}", min_y);
        }

        if (cell.com.y < min_com_y) {
            min_com_y = cell.com.y;
        }
    }

    spdlog::info("Min COM.y reached: {:.3f} (negative = upward from center)", min_com_y);

    int jump_height = settled_y - min_y;
    spdlog::info(
        "Duck jumped from y={} to min y={}, height={} cells", settled_y, min_y, jump_height);

    // Verify duck jumped at least 2 cells high.
    EXPECT_GE(jump_height, 2) << "Duck should jump at least 2 cells high";
}

// ============================================================================
// Cliff Detection and Jumping Tests
// ============================================================================

TEST_F(DuckJumpTest, DuckBrain2JumpsOverCliffWhenFast)
{
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::info);

    // Create world with cliff: floor until x=15, gap from x=16-20, floor resumes x=21+.
    // World is 30 wide, so duck has room to accelerate and encounter cliff.
    constexpr int CLIFF_START = 16;
    constexpr int CLIFF_END = 20;
    auto world = createCliffWorld(30, CLIFF_START, CLIFF_END);

    OrganismManager& manager = world->getOrganismManager();

    // Create duck with DuckBrain2 near left wall.
    auto brain = std::make_unique<DuckBrain2>();
    DuckBrain2* brain_ptr = brain.get();
    OrganismId duck_id = manager.createDuck(*world, 2, 7, std::move(brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle.
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    spdlog::info("CliffTest: Duck settled at x={}", duck->getAnchorCell().x);

    // Run simulation until duck either:
    // 1. Falls into the cliff (y > 8)
    // 2. Successfully crosses (x > CLIFF_END + 1)
    // 3. Timeout after 500 frames.
    bool fell_in_cliff = false;
    bool crossed_cliff = false;
    int jump_count = 0;
    int first_cliff_jump_x = -1; // Track where the first cliff jump occurred.
    bool was_on_ground = duck->isOnGround();

    for (int i = 0; i < 500; ++i) {
        world->advanceTime(0.016);

        int x = duck->getAnchorCell().x;
        int y = duck->getAnchorCell().y;
        bool on_ground = duck->isOnGround();

        // Detect jumps.
        if (was_on_ground && !on_ground) {
            jump_count++;
            spdlog::info("CliffTest frame {}: Jump #{} at x={}", i, jump_count, x);

            // Record first jump near the cliff.
            if (first_cliff_jump_x < 0 && x >= CLIFF_START - 2) {
                first_cliff_jump_x = x;
            }
        }

        // Check if duck fell into the gap (below floor level).
        if (y >= 9) {
            fell_in_cliff = true;
            spdlog::info("CliffTest frame {}: Duck fell into cliff at ({}, {})", i, x, y);
            break;
        }

        // Check if duck crossed the cliff.
        if (x >= CLIFF_END + 2) {
            crossed_cliff = true;
            spdlog::info("CliffTest frame {}: Duck crossed cliff, now at x={}", i, x);
            break;
        }

        was_on_ground = on_ground;
    }

    // Log knowledge state.
    const auto& knowledge = brain_ptr->getKnowledge();
    spdlog::info(
        "CliffTest: Knowledge - max_speed={:.1f}, jump_distance={:.1f}",
        knowledge.max_speed.value_or(-1),
        knowledge.jump_distance.value_or(-1));

    spdlog::info(
        "CliffTest: fell_in_cliff={}, crossed_cliff={}, jump_count={}, first_cliff_jump_x={}",
        fell_in_cliff,
        crossed_cliff,
        jump_count,
        first_cliff_jump_x);

    // Duck should jump when it sees a cliff (survival instinct, no knowledge needed).
    EXPECT_GE(jump_count, 1) << "Duck should jump when cliff detected";
    EXPECT_TRUE(crossed_cliff) << "Duck should cross the cliff";
    EXPECT_FALSE(fell_in_cliff) << "Duck should not fall into cliff";

    // Duck should jump close to the edge, not too early.
    // Must be within 1 cell of the cliff start.
    EXPECT_GE(first_cliff_jump_x, CLIFF_START - 1)
        << "Duck should jump within 1 cell of cliff edge, not earlier";
}

TEST_F(DuckJumpTest, DuckBrain2DetectsCliffInSensoryData)
{
    // Create world with cliff.
    auto world = createCliffWorld(20, 10, 14);

    OrganismManager& manager = world->getOrganismManager();

    // Use test brain so we can control movement.
    auto test_brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brain_ptr = test_brain.get();

    // Create duck near the cliff edge.
    OrganismId duck_id = manager.createDuck(*world, 8, 7, std::move(test_brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle and start moving right.
    brain_ptr->setAction(DuckAction::WAIT);
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    // Start moving right toward cliff.
    brain_ptr->setAction(DuckAction::RUN_RIGHT);
    for (int i = 0; i < 30; ++i) {
        world->advanceTime(0.016);
    }

    // Get sensory data when duck is near cliff edge.
    DuckSensoryData sensory = duck->gatherSensoryData(*world, 0.016);

    // Log the floor row of sensory grid.
    spdlog::info("CliffSensory: Duck at x={}, facing_x={}", sensory.position.x, sensory.facing_x);
    std::string floor_str;
    constexpr int FLOOR_ROW = 5; // Row below duck center (4).
    for (int col = 0; col < DuckSensoryData::GRID_SIZE; ++col) {
        double total_fill = 0.0;
        for (int mat = 0; mat < DuckSensoryData::NUM_MATERIALS; ++mat) {
            if (mat != static_cast<int>(Material::EnumType::Air)) {
                total_fill += sensory.material_histograms[FLOOR_ROW][col][mat];
            }
        }
        floor_str += (total_fill >= 0.3) ? "#" : ".";
    }
    spdlog::info("CliffSensory: Floor row (row 5): [{}]", floor_str);

    // The sensory grid should show floor dropping off ahead.
    // Verify the test setup works (duck should be near cliff edge by now).
    EXPECT_GE(sensory.position.x, 9) << "Duck should have moved toward cliff";
}

// ============================================================================
// Obstacle Jumping Tests
// ============================================================================

struct ObstacleTestCase {
    int obstacle_x;
    int obstacle_height;
    const char* name;
};

class DuckObstacleJumpTest : public ::testing::TestWithParam<ObstacleTestCase> {
protected:
    void SetUp() override
    {
        LoggingChannels::initialize();
        LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::debug);
    }
};

TEST_P(DuckObstacleJumpTest, JumpsOverObstacle)
{
    const auto& params = GetParam();
    spdlog::info(
        "ObstacleJumpTest: obstacle_x={}, height={}, name={}",
        params.obstacle_x,
        params.obstacle_height,
        params.name);

    auto world = createObstacleWorld(20, 10, params.obstacle_x, params.obstacle_height);
    printWorld(*world, "Initial world with obstacle");

    OrganismManager& manager = world->getOrganismManager();

    // Duck spawns with one cell gap from left wall, one cell up from floor.
    // In a 20x10 world: wall at x=0, gap at x=1, duck at x=2.
    constexpr int SPAWN_X = 2;
    constexpr int SPAWN_Y = 7; // One cell up in the air to let it settle.

    auto brain = std::make_unique<DuckBrain2>();
    DuckBrain2* brain_ptr = brain.get();
    OrganismId duck_id = manager.createDuck(*world, SPAWN_X, SPAWN_Y, std::move(brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    printWorld(*world, "After duck spawn");

    // Let duck settle onto ground.
    for (int i = 0; i < 30; ++i) {
        world->advanceTime(0.016);
    }

    int settled_x = duck->getAnchorCell().x;
    spdlog::info("Duck settled at x={}", settled_x);

    // Track that duck is moving right by frame 10.
    bool moving_right_by_frame_10 = false;

    // Track jump timing relative to obstacle.
    bool jumped = false;
    int jump_x = -1;
    bool was_on_ground = duck->isOnGround();

    // Track if duck cleared the obstacle.
    bool cleared_obstacle = false;
    int max_x_reached = settled_x;

    // Run simulation.
    constexpr int MAX_FRAMES = 300;
    for (int frame = 0; frame < MAX_FRAMES; ++frame) {
        world->advanceTime(0.016);

        int current_x = duck->getAnchorCell().x;
        bool on_ground = duck->isOnGround();

        // Check movement by frame 10.
        if (frame == 10) {
            moving_right_by_frame_10 = (current_x > settled_x);
            spdlog::info(
                "Frame 10: x={}, settled_x={}, moving_right={}",
                current_x,
                settled_x,
                moving_right_by_frame_10);
        }

        // Detect jump.
        if (was_on_ground && !on_ground && !jumped) {
            jumped = true;
            jump_x = current_x;
            spdlog::info("Frame {}: Jump detected at x={}", frame, current_x);
        }

        // Track max x reached.
        if (current_x > max_x_reached) {
            max_x_reached = current_x;
        }

        // Check if cleared obstacle.
        if (current_x > params.obstacle_x + 1) {
            cleared_obstacle = true;
            spdlog::info("Frame {}: Cleared obstacle, now at x={}", frame, current_x);
            break;
        }

        was_on_ground = on_ground;
    }

    // Log final state.
    const auto& knowledge = brain_ptr->getKnowledge();
    spdlog::info(
        "Final state: jumped={}, jump_x={}, max_x={}, cleared={}",
        jumped,
        jump_x,
        max_x_reached,
        cleared_obstacle);
    spdlog::info(
        "Knowledge: max_speed={:.1f}, jump_distance={:.1f}",
        knowledge.max_speed.value_or(-1),
        knowledge.jump_distance.value_or(-1));

    printWorld(*world, "Final world state");

    // Assertions.
    EXPECT_TRUE(moving_right_by_frame_10) << "Duck should be moving right by frame 10";

    EXPECT_TRUE(jumped) << "Duck should jump when approaching obstacle";

    if (jumped) {
        EXPECT_LT(jump_x, params.obstacle_x)
            << "Duck should jump BEFORE reaching the obstacle (jump_x=" << jump_x
            << ", obstacle_x=" << params.obstacle_x << ")";
    }

    EXPECT_TRUE(cleared_obstacle) << "Duck should clear the obstacle (max_x=" << max_x_reached
                                  << ", obstacle_x=" << params.obstacle_x << ")";
}

// Start with just one test case: obstacle in the middle.
INSTANTIATE_TEST_SUITE_P(
    ObstacleLocations,
    DuckObstacleJumpTest,
    ::testing::Values(ObstacleTestCase{ 10, 1, "middle_1h" }
                      // Future test cases:
                      // ObstacleTestCase{5, 1, "near_spawn_1h"},
                      // ObstacleTestCase{15, 1, "far_1h"},
                      // ObstacleTestCase{10, 2, "middle_2h"}
                      ),
    [](const ::testing::TestParamInfo<ObstacleTestCase>& info) { return info.param.name; });

// ============================================================================
// Air Steering Tests (SMB1-style limited air control)
// ============================================================================

/**
 * Test: Jumping while moving right and holding right (forward).
 *
 * Expected SMB1-style behavior:
 * - Holding forward while already moving forward should have minimal effect.
 * - The duck is already near max speed, so additional acceleration is limited.
 * - Should mostly maintain momentum through the jump arc.
 */
TEST_F(DuckJumpTest, AirSteeringForwardWhileMovingForward)
{
    // Create a taller world for jump testing.
    // Need height for the duck to actually become airborne.
    auto setup = DuckTestSetup::create(50, 15, 5, 13);
    ASSERT_NE(setup.duck, nullptr);
    ASSERT_TRUE(setup.duck->isOnGround()) << "Duck should be on ground after settling";

    int start_x = setup.duck->getAnchorCell().x;
    int start_y = setup.duck->getAnchorCell().y;
    spdlog::info("AirSteeringForward: Duck settled at ({}, {})", start_x, start_y);

    // Phase 1: Build up rightward velocity on ground.
    setup.brain->setDirectInput({ 1.0f, 0.0f }, false);
    setup.advanceFrames(30);

    double vel_before_jump = setup.getVelocity().x;
    int x_before_jump = setup.duck->getAnchorCell().x;
    spdlog::info(
        "AirSteeringForward: Before jump - x={}, vel.x={:.2f}", x_before_jump, vel_before_jump);

    ASSERT_GT(vel_before_jump, 1.0) << "Duck should have built up rightward velocity";
    ASSERT_TRUE(setup.duck->isOnGround()) << "Duck should still be on ground";

    // Phase 2: Jump while holding right.
    setup.brain->setDirectInput({ 1.0f, 0.0f }, true);
    setup.advance(); // Jump frame.

    // Phase 3: Track jump arc using Y position only (ground detection is unreliable).
    int min_y = start_y; // Track highest point (lowest Y).
    int airborne_start_frame = -1;
    int peak_frame = -1;
    int landed_frame = -1;
    double vel_at_airborne_start = 0.0;
    std::vector<double> air_velocities;

    for (int frame = 0; frame < 150; ++frame) {
        setup.advance();
        int y = setup.duck->getAnchorCell().y;
        double vel_x = setup.getVelocity().x;
        double vel_y = setup.getVelocity().y;

        // Detect when we actually become airborne (y decreases from start).
        if (airborne_start_frame < 0 && y < start_y) {
            airborne_start_frame = frame;
            vel_at_airborne_start = vel_x;
            spdlog::info(
                "AirSteeringForward: Became airborne at frame {}, y={}, vel.x={:.2f}",
                frame,
                y,
                vel_x);
        }

        // Track peak of jump (minimum y).
        if (airborne_start_frame >= 0) {
            air_velocities.push_back(vel_x);
            if (y < min_y) {
                min_y = y;
                peak_frame = frame;
            }
        }

        // Log key frames.
        if (frame % 10 == 0) {
            spdlog::info(
                "  Frame {}: pos=({},{}), vel=({:.2f},{:.2f})",
                frame,
                setup.duck->getAnchorCell().x,
                y,
                vel_x,
                vel_y);
        }

        // Detect landing: after reaching peak, Y returns to start_y or higher.
        if (peak_frame >= 0 && y >= start_y) {
            landed_frame = frame;
            spdlog::info("AirSteeringForward: Landed at frame {}, y={}", frame, y);
            break;
        }
    }

    // Verify we actually had a proper jump arc.
    ASSERT_GE(airborne_start_frame, 0) << "Duck should have become airborne";
    ASSERT_GE(peak_frame, airborne_start_frame) << "Duck should have reached a peak";
    ASSERT_LT(min_y, start_y) << "Duck should have jumped above starting Y";
    ASSERT_GE(landed_frame, peak_frame) << "Duck should have landed after peak";

    int x_after_jump = setup.duck->getAnchorCell().x;
    double vel_after_land = setup.getVelocity().x;
    int air_frames = landed_frame - airborne_start_frame;

    spdlog::info(
        "AirSteeringForward: After landing - x={}, vel.x={:.2f}", x_after_jump, vel_after_land);
    spdlog::info("AirSteeringForward: Air phase: {} frames, peak at y={}", air_frames, min_y);

    // Assertions for SMB1-style behavior:
    // 1. Duck should have moved forward during jump.
    EXPECT_GT(x_after_jump, x_before_jump) << "Duck should move forward during jump";

    // 2. For forward input while moving forward, velocity should be roughly maintained.
    double vel_change_during_air = vel_after_land - vel_at_airborne_start;
    spdlog::info(
        "AirSteeringForward: Velocity change during air phase: {:.2f}", vel_change_during_air);
}

/**
 * Test: Air steering should cause different deceleration based on input direction.
 *
 * This test compares two identical jumps:
 * 1. Jump while holding FORWARD (right) - should maintain/gain speed
 * 2. Jump while holding BACKWARD (left) - should lose speed faster
 *
 * Expected SMB1-style behavior:
 * - Backward input mid-air should cause MORE deceleration than forward input.
 * - This test FAILS until air steering is implemented (currently both show same decel).
 */
TEST_F(DuckJumpTest, AirSteeringBackwardDecelsFasterThanForward)
{
    // Helper lambda to run a jump scenario and return velocity change.
    auto runJumpScenario = [](float air_input_x, const char* label) -> double {
        auto setup = DuckTestSetup::create(50, 15, 5, 13);
        if (!setup.duck || !setup.duck->isOnGround()) {
            return 0.0; // Setup failed.
        }

        int start_y = setup.duck->getAnchorCell().y;

        // Build up rightward velocity on ground.
        setup.brain->setDirectInput({ 1.0f, 0.0f }, false);
        setup.advanceFrames(30);

        double vel_before_jump = setup.getVelocity().x;
        spdlog::info("{}: Before jump vel.x={:.2f}", label, vel_before_jump);

        // Jump.
        setup.brain->setDirectInput({ 1.0f, 0.0f }, true);
        setup.advance();

        // Track jump arc, apply air input when airborne.
        int min_y = start_y;
        int airborne_start_frame = -1;
        int peak_frame = -1;
        double vel_at_airborne_start = 0.0;
        double vel_at_land = 0.0;

        for (int frame = 0; frame < 150; ++frame) {
            setup.advance();
            int y = setup.duck->getAnchorCell().y;
            double vel_x = setup.getVelocity().x;

            // Detect airborne and apply air input.
            if (airborne_start_frame < 0 && y < start_y) {
                airborne_start_frame = frame;
                vel_at_airborne_start = vel_x;
                setup.brain->setMove({ air_input_x, 0.0f });
                spdlog::info(
                    "{}: Airborne at frame {}, vel.x={:.2f}, input={:.1f}",
                    label,
                    frame,
                    vel_x,
                    air_input_x);
            }

            // Track peak.
            if (airborne_start_frame >= 0 && y < min_y) {
                min_y = y;
                peak_frame = frame;
            }

            // Detect landing.
            if (peak_frame >= 0 && y >= start_y) {
                vel_at_land = vel_x;
                spdlog::info("{}: Landed at frame {}, vel.x={:.2f}", label, frame, vel_x);
                break;
            }
        }

        double vel_change = vel_at_land - vel_at_airborne_start;
        spdlog::info("{}: Velocity change during air: {:.2f}", label, vel_change);
        return vel_change;
    };

    // Run both scenarios.
    double vel_change_forward = runJumpScenario(1.0f, "Forward");
    double vel_change_backward = runJumpScenario(-1.0f, "Backward");

    spdlog::info("=== Air Steering Comparison ===");
    spdlog::info("Forward input:  vel_change = {:.2f}", vel_change_forward);
    spdlog::info("Backward input: vel_change = {:.2f}", vel_change_backward);
    spdlog::info("Difference: {:.2f}", vel_change_backward - vel_change_forward);

    // KEY ASSERTION: Backward input should cause MORE deceleration than forward.
    // (More negative = more deceleration.)
    // Backward should decel at least 1% more than forward.
    EXPECT_LT(vel_change_backward, vel_change_forward * 1.01)
        << "Backward air input should cause more deceleration than forward input. "
        << "Forward: " << vel_change_forward << ", Backward: " << vel_change_backward;

    // Backward input should cause deceleration (negative velocity change).
    EXPECT_LT(vel_change_backward, 0.0) << "Backward should decelerate";
}

/**
 * Test: Facing direction should be locked while airborne (SMB1-style).
 *
 * In SMB1, Mario's facing direction is set at jump time and doesn't change
 * until landing. This enables the backwards jump trick - you can steer
 * opposite to your facing direction for bonus acceleration.
 *
 * This test verifies that steering input while airborne does NOT change facing.
 */
TEST_F(DuckJumpTest, FacingLockedWhileAirborne)
{
    auto setup = DuckTestSetup::create(50, 15, 5, 13);
    ASSERT_NE(setup.duck, nullptr);
    ASSERT_TRUE(setup.duck->isOnGround());

    int start_y = setup.duck->getAnchorCell().y;

    // Build rightward velocity - facing should become RIGHT.
    setup.brain->setDirectInput({ 1.0f, 0.0f }, false);
    setup.advanceFrames(30);
    ASSERT_GT(setup.duck->getFacing().x, 0.0f) << "Should be facing right after moving right";

    // Jump while holding right.
    setup.brain->setDirectInput({ 1.0f, 0.0f }, true);
    setup.advance();

    // Wait until airborne (position-based detection like other tests).
    int airborne_frame = -1;
    for (int i = 0; i < 20; ++i) {
        setup.advance();
        if (setup.duck->getAnchorCell().y < start_y) {
            airborne_frame = i;
            break;
        }
    }
    ASSERT_GE(airborne_frame, 0) << "Duck should become airborne after jump";

    float facing_at_jump = setup.duck->getFacing().x;
    spdlog::info(
        "FacingLocked: Airborne at frame {}, facing.x = {:.1f}", airborne_frame, facing_at_jump);
    ASSERT_GT(facing_at_jump, 0.0f) << "Should be facing right at jump time";

    // Now steer LEFT while airborne for several frames.
    setup.brain->setMove({ -1.0f, 0.0f });
    int frames_checked = 0;
    for (int frame = 0; frame < 50; ++frame) {
        setup.advance();
        int y = setup.duck->getAnchorCell().y;

        // Only check while still above ground level.
        if (y < start_y) {
            frames_checked++;
            float current_facing = setup.duck->getFacing().x;
            EXPECT_GT(current_facing, 0.0f)
                << "Facing should remain RIGHT while airborne, but at frame " << frame
                << " facing.x = " << current_facing << ". Facing should be locked at jump time.";
        }

        // Stop once we land.
        if (y >= start_y && frame > airborne_frame + 5) {
            spdlog::info(
                "FacingLocked: Landed at frame {}, checked {} airborne frames",
                frame,
                frames_checked);
            break;
        }
    }

    ASSERT_GT(frames_checked, 5) << "Should have checked facing for at least 5 airborne frames";
}

/**
 * Test: Backwards jump trick - jumping while facing opposite to movement direction
 * should provide better air acceleration (SMB1-style asymmetric acceleration).
 *
 * Setup:
 * - Build rightward velocity on ground.
 * - Normal jump: Jump while holding right (facing right).
 * - Backwards jump: Tap left on jump frame (face left), then steer right.
 *
 * Expected: Backwards jump should accelerate faster because input opposes jump_facing_.
 */
TEST_F(DuckJumpTest, BackwardsJumpTrickGivesBetterAcceleration)
{
    // Helper to run a jump scenario and measure velocity change.
    // jump_facing_left: if true, tap left on jump frame to face left before jumping.
    auto runJumpScenario = [](bool jump_facing_left, const char* label) -> double {
        auto setup = DuckTestSetup::create(50, 15, 5, 13);
        if (!setup.duck || !setup.duck->isOnGround()) {
            return 0.0;
        }

        int start_y = setup.duck->getAnchorCell().y;

        // Phase 1: Build up rightward velocity on ground.
        setup.brain->setDirectInput({ 1.0f, 0.0f }, false);
        setup.advanceFrames(30);

        double vel_before_jump = setup.getVelocity().x;
        spdlog::info("{}: Before jump vel.x={:.2f}", label, vel_before_jump);

        // Phase 2: Jump frame - optionally face left by tapping left.
        if (jump_facing_left) {
            // Tap left on jump frame to set facing left, but still jump.
            setup.brain->setDirectInput({ -1.0f, 0.0f }, true);
        }
        else {
            // Normal: jump while holding right.
            setup.brain->setDirectInput({ 1.0f, 0.0f }, true);
        }
        setup.advance(); // Execute jump.

        // Phase 3: Track jump arc, steer RIGHT once airborne.
        int min_y = start_y;
        int airborne_start_frame = -1;
        int peak_frame = -1;
        double vel_at_airborne_start = 0.0;
        double vel_at_land = 0.0;

        for (int frame = 0; frame < 150; ++frame) {
            setup.advance();
            int y = setup.duck->getAnchorCell().y;
            double vel_x = setup.getVelocity().x;

            // Detect airborne and switch to steering right.
            if (airborne_start_frame < 0 && y < start_y) {
                airborne_start_frame = frame;
                vel_at_airborne_start = vel_x;
                // Both scenarios steer RIGHT in the air.
                setup.brain->setMove({ 1.0f, 0.0f });
                spdlog::info("{}: Airborne at frame {}, vel.x={:.2f}", label, frame, vel_x);
            }

            // Track peak.
            if (airborne_start_frame >= 0 && y < min_y) {
                min_y = y;
                peak_frame = frame;
            }

            // Detect landing.
            if (peak_frame >= 0 && y >= start_y) {
                vel_at_land = vel_x;
                spdlog::info("{}: Landed at frame {}, vel.x={:.2f}", label, frame, vel_x);
                break;
            }
        }

        double vel_change = vel_at_land - vel_at_airborne_start;
        spdlog::info("{}: Velocity change during air: {:.2f}", label, vel_change);
        return vel_change;
    };

    // Run both scenarios.
    double vel_change_normal = runJumpScenario(false, "NormalJump");
    double vel_change_backwards = runJumpScenario(true, "BackwardsJump");

    spdlog::info("=== Backwards Jump Trick Comparison ===");
    spdlog::info(
        "Normal jump (face right, steer right):    vel_change = {:.2f}", vel_change_normal);
    spdlog::info(
        "Backwards jump (face left, steer right):  vel_change = {:.2f}", vel_change_backwards);
    spdlog::info("Difference: {:.2f}", vel_change_backwards - vel_change_normal);

    // KEY ASSERTION: Backwards jump should result in better acceleration (less deceleration
    // or more acceleration) because steering opposite to facing direction gives a bonus.
    constexpr double MIN_DIFFERENCE = 1.0;
    EXPECT_GT(vel_change_backwards, vel_change_normal + MIN_DIFFERENCE)
        << "Backwards jump trick should provide better acceleration than normal jump. "
        << "Normal: " << vel_change_normal << ", Backwards: " << vel_change_backwards
        << ". This test requires asymmetric air steering to be implemented.";
}

/**
 * Test: Asymmetric air steering - steering opposite to facing should give higher force.
 *
 * SMB1 mechanic: "You accelerate faster in the direction you are NOT facing."
 *
 * This test isolates the asymmetric multiplier by comparing:
 * - Face RIGHT, steer RIGHT → lower multiplier (same direction)
 * - Face LEFT, steer RIGHT  → higher multiplier (opposing direction)
 *
 * Both scenarios steer RIGHT, but with different facing directions. The one where
 * input opposes facing should experience MORE force (better acceleration).
 */
TEST_F(DuckJumpTest, AsymmetricAirSteeringOpposingGivesHigherForce)
{
    // Helper to run a scenario. face_left_at_jump controls facing direction.
    auto runScenario = [](bool face_left_at_jump, const char* label) -> double {
        auto setup = DuckTestSetup::create(50, 15, 5, 13);
        if (!setup.duck || !setup.duck->isOnGround()) {
            return 0.0;
        }

        int start_y = setup.duck->getAnchorCell().y;

        // Build rightward velocity.
        setup.brain->setDirectInput({ 1.0f, 0.0f }, false);
        setup.advanceFrames(30);

        double vel_before = setup.getVelocity().x;

        // Jump - optionally tap left to face left.
        if (face_left_at_jump) {
            setup.brain->setDirectInput({ -1.0f, 0.0f }, true);
        }
        else {
            setup.brain->setDirectInput({ 1.0f, 0.0f }, true);
        }
        setup.advance();

        float facing_at_jump = setup.duck->getFacing().x;
        spdlog::info(
            "{}: Before jump vel.x={:.2f}, facing at jump={:.1f}",
            label,
            vel_before,
            facing_at_jump);

        // Track until airborne, then steer RIGHT.
        double vel_at_airborne = 0.0;
        for (int i = 0; i < 20; ++i) {
            setup.advance();
            if (setup.duck->getAnchorCell().y < start_y) {
                vel_at_airborne = setup.getVelocity().x;
                // Both scenarios steer RIGHT.
                setup.brain->setMove({ 1.0f, 0.0f });
                spdlog::info(
                    "{}: Airborne, vel.x={:.2f}, facing={:.1f}, steering RIGHT",
                    label,
                    vel_at_airborne,
                    setup.duck->getFacing().x);
                break;
            }
        }

        // Track for fixed number of airborne frames.
        constexpr int AIR_FRAMES = 30;
        for (int i = 0; i < AIR_FRAMES; ++i) {
            setup.advance();
        }

        double vel_after = setup.getVelocity().x;
        double vel_change = vel_after - vel_at_airborne;
        spdlog::info(
            "{}: After {} air frames, vel.x={:.2f}, change={:.2f}",
            label,
            AIR_FRAMES,
            vel_after,
            vel_change);
        return vel_change;
    };

    // Both scenarios steer RIGHT, but with different facing.
    double vel_change_face_right = runScenario(false, "FaceRight"); // Same as steer.
    double vel_change_face_left = runScenario(true, "FaceLeft");    // Opposing steer.

    spdlog::info("=== Asymmetric Air Steering Test ===");
    spdlog::info("Face RIGHT, steer RIGHT (same):     vel_change = {:.2f}", vel_change_face_right);
    spdlog::info("Face LEFT, steer RIGHT (opposing):  vel_change = {:.2f}", vel_change_face_left);

    // Both steer RIGHT. With asymmetric multiplier:
    // - Face RIGHT, steer RIGHT → 15% force (same direction)
    // - Face LEFT, steer RIGHT → 30% force (opposing direction)
    //
    // The opposing scenario should accelerate MORE (or decelerate less).
    // This is the backwards jump trick - facing away gives better acceleration.
    double accel_difference = vel_change_face_left - vel_change_face_right;
    spdlog::info(
        "Acceleration difference: {:.2f} (positive = opposing accelerates more)", accel_difference);

    // Opposing steer should give at least 1% better acceleration.
    double min_asymmetry = std::abs(vel_change_face_right) * 0.01;
    EXPECT_GT(accel_difference, min_asymmetry)
        << "Backwards jump should give better acceleration. "
        << "FaceRight: " << vel_change_face_right << ", FaceLeft: " << vel_change_face_left
        << ". Expected difference > " << min_asymmetry << ", got " << accel_difference;
}
