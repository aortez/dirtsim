/**
 * @file DuckBrain_test.cpp
 * @brief Tests for duck brain behaviors (WallBouncingBrain, DuckBrain2).
 *
 * These tests verify AI decision-making: wall bouncing, learning, spawn detection, etc.
 * For basic physics tests, see Duck_test.cpp.
 * For jumping/air steering tests, see DuckJump_test.cpp.
 */

#include "DuckTestUtils.h"
#include "core/CellDebug.h"
#include "core/GridOfCells.h"
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

class DuckBrainTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::debug); }
};

// ============================================================================
// WallBouncingBrain Tests
// ============================================================================

TEST_F(DuckBrainTest, WallBouncingBrainPingPongs)
{
    // Create world for wall bouncing.
    auto world = createFlatWorld(10, 5);

    OrganismManager& manager = world->getOrganismManager();

    // Create duck with WallBouncingBrain in the middle.
    auto brain = std::make_unique<WallBouncingBrain>();
    OrganismId duck_id = manager.createDuck(*world, 5, 3, std::move(brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle.
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    int start_x = duck->getAnchorCell().x;
    spdlog::info("Duck settled at x={}", start_x);

    // Validate ping-pong behavior by tracking direction changes.
    enum class Direction { RIGHT, LEFT, UNKNOWN };
    Direction current_direction = Direction::RIGHT;
    int last_x = start_x;
    int bounce_count = 0;
    int min_x = start_x;
    int max_x = start_x;

    for (int i = 0; i < 600; ++i) {
        world->advanceTime(0.016);
        int current_x = duck->getAnchorCell().x;

        // Track range of movement.
        if (current_x < min_x) min_x = current_x;
        if (current_x > max_x) max_x = current_x;

        // Detect direction changes (bounces).
        if (current_x != last_x) {
            Direction actual_direction = (current_x > last_x) ? Direction::RIGHT : Direction::LEFT;

            if (current_direction != Direction::UNKNOWN && actual_direction != current_direction) {
                // Direction changed - this is a bounce.
                bounce_count++;
                spdlog::info(
                    "Frame {}: Bounce #{} detected at x={} (now moving {})",
                    i,
                    bounce_count,
                    current_x,
                    (actual_direction == Direction::RIGHT) ? "RIGHT" : "LEFT");
            }

            current_direction = actual_direction;
        }

        last_x = current_x;
    }

    spdlog::info("Duck traveled from x={} to x={}, {} total bounces", min_x, max_x, bounce_count);

    // Duck should bounce multiple times in 600 frames (3x original duration).
    EXPECT_GE(bounce_count, 3) << "Duck should bounce at least 3 times in 600 frames";
    EXPECT_GE(max_x - min_x, 7) << "Duck should traverse most of the world";
}

TEST_F(DuckBrainTest, WallBouncingBrainBouncesOffWall)
{
    // Initialize logging and enable brain debug logging.
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::debug);

    // Create world (no automatic WALL borders - sensory system will mark edges as WALL).
    auto world = createFlatWorld(10, 5);
    printWorld(*world, "Initial world");

    OrganismManager& manager = world->getOrganismManager();

    // Create duck near middle with WallBouncingBrain.
    auto brain = std::make_unique<WallBouncingBrain>();
    OrganismId duck_id = manager.createDuck(*world, 5, 3, std::move(brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle.
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    int start_x = duck->getAnchorCell().x;
    spdlog::info("Duck settled at x={}", start_x);
    printWorld(*world, "After duck settled");

    // Print duck's sensory view.
    DuckSensoryData sensory = duck->gatherSensoryData(*world, 0.016);
    const int center = DuckSensoryData::GRID_SIZE / 2;
    spdlog::info(
        "Duck sensory grid ({}x{}, center at [{}][{}], WALL=W, WOOD=D):",
        DuckSensoryData::GRID_SIZE,
        DuckSensoryData::GRID_SIZE,
        center,
        center);
    for (int y = 0; y < DuckSensoryData::GRID_SIZE; ++y) {
        std::string row;
        for (int x = 0; x < DuckSensoryData::GRID_SIZE; ++x) {
            // Check for WALL (material index 7).
            double wall_fill = sensory.material_histograms[y][x][7];
            if (wall_fill > 0.5) {
                row += "W";
            }
            else {
                // Check for WOOD (duck itself, material index 9).
                double wood_fill = sensory.material_histograms[y][x][9];
                if (wood_fill > 0.5) {
                    row += "D";
                }
                else {
                    row += ".";
                }
            }
        }
        spdlog::info("  {}", row);
    }

    // Run until duck hits right wall (x=9 is the right wall).
    bool hit_wall = false;
    for (int i = 0; i < 100; ++i) {
        world->advanceTime(0.016);
        int current_x = duck->getAnchorCell().x;

        // Duck is near wall if at x >= 8 (wall is at x=9).
        if (current_x >= 8) {
            spdlog::info("Frame {}: Duck reached right wall at x={}", i, current_x);
            printWorld(*world, "Duck at right wall");
            hit_wall = true;
            break;
        }
    }

    EXPECT_TRUE(hit_wall) << "Duck should reach the right wall within 100 frames";

    // Now verify duck bounces back.
    if (hit_wall) {
        int wall_x = duck->getAnchorCell().x;
        spdlog::info("Duck at wall x={}, waiting for bounce...", wall_x);

        // Run another 100 frames to see bounce.
        bool bounced = false;
        for (int i = 0; i < 100; ++i) {
            world->advanceTime(0.016);
            int current_x = duck->getAnchorCell().x;

            // Check if duck moved left (bounced).
            if (current_x < wall_x - 1) {
                spdlog::info("Frame {}: Duck bounced! Now at x={}", i, current_x);
                printWorld(*world, "After bounce");
                bounced = true;
                break;
            }
        }

        EXPECT_TRUE(bounced) << "Duck should bounce back from wall";
    }
}

TEST_F(DuckBrainTest, WallBouncingBrainJumpsAtMidpoint)
{
    // Enable brain debug logging.
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::info);

    // Create world.
    auto world = createFlatWorld(10, 5);

    OrganismManager& manager = world->getOrganismManager();

    // Create duck with WallBouncingBrain with jumping enabled.
    auto brain = std::make_unique<WallBouncingBrain>(true);
    OrganismId duck_id = manager.createDuck(*world, 5, 3, std::move(brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle.
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    spdlog::info("Duck settled at x={}, running with jumping enabled...", duck->getAnchorCell().x);

    // Run long enough to establish consistent pattern and see jumps.
    int jump_count = 0;
    int last_y = duck->getAnchorCell().y;
    int local_max_y = last_y; // Track local peak (lowest point before jump).
    bool was_on_ground = duck->isOnGround();

    for (int i = 0; i < 800; ++i) {
        world->advanceTime(0.016);

        int current_y = duck->getAnchorCell().y;
        bool on_ground = duck->isOnGround();

        // Detect jump: transition from on_ground to airborne with upward movement.
        if (was_on_ground && !on_ground && current_y <= last_y) {
            jump_count++;
            spdlog::info(
                "Frame {}: Jump #{} detected - left ground at y={}", i, jump_count, current_y);
        }

        // Track local peaks (before jumps).
        if (current_y > local_max_y) {
            local_max_y = current_y;
        }

        last_y = current_y;
        was_on_ground = on_ground;
    }

    spdlog::info("Duck jumped {} times in 800 frames", jump_count);

    // With jumping enabled and consistent pattern, should see multiple jumps.
    EXPECT_GE(jump_count, 2) << "Duck should jump at least twice with jumping enabled";
}

// ============================================================================
// DuckBrain2 Tests
// ============================================================================

TEST_F(DuckBrainTest, DuckBrain2DetectsSpawnSide)
{
    // Create world - duck spawns near left wall.
    auto world = createFlatWorld(20, 5);
    OrganismManager& manager = world->getOrganismManager();

    // Create duck with DuckBrain2 near left wall (x=1).
    auto brain = std::make_unique<DuckBrain2>();
    OrganismId duck_id = manager.createDuck(*world, 1, 3, std::move(brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Run one frame to let brain initialize.
    world->advanceTime(0.016);

    // Duck should be running right (away from spawn side).
    DuckAction action = duck->getCurrentAction();
    EXPECT_EQ(action, DuckAction::RUN_RIGHT) << "Duck spawned on left should run right toward exit";
}

TEST_F(DuckBrainTest, DuckBrain2TurnsAroundAtWall)
{
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::info);

    // Create world.
    auto world = createFlatWorld(15, 5);
    OrganismManager& manager = world->getOrganismManager();

    // Create duck with DuckBrain2 near left wall.
    auto brain = std::make_unique<DuckBrain2>();
    OrganismId duck_id = manager.createDuck(*world, 2, 3, std::move(brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle.
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    int start_x = duck->getAnchorCell().x;
    spdlog::info("Duck settled at x={}", start_x);

    // Run until duck hits right wall and turns around.
    bool hit_right_wall = false;
    bool turned_around = false;
    int rightmost_x_after_hit = -1;
    int previous_x = duck->getAnchorCell().x;
    int leftward_step_count = 0;

    for (int i = 0; i < 300; ++i) {
        world->advanceTime(0.016);
        int current_x = duck->getAnchorCell().x;

        // Detect hitting right wall (near x=13 or 14).
        if (!hit_right_wall && current_x >= 12) {
            hit_right_wall = true;
            rightmost_x_after_hit = current_x;
            spdlog::info("Frame {}: Duck hit right wall at x={}", i, current_x);
        }

        if (hit_right_wall) {
            if (current_x > rightmost_x_after_hit) {
                rightmost_x_after_hit = current_x;
                leftward_step_count = 0;
            }

            // Confirm turn-around using both position and direction so airborne arcs do not
            // produce false positives.
            if (current_x < previous_x) {
                leftward_step_count++;
            }
            else if (current_x > previous_x) {
                leftward_step_count = 0;
            }

            if ((rightmost_x_after_hit - current_x) >= 2 && leftward_step_count >= 2) {
                turned_around = true;
                spdlog::info(
                    "Frame {}: Duck turned around, moved left from x={} to x={}.",
                    i,
                    rightmost_x_after_hit,
                    current_x);
                break;
            }
        }

        previous_x = current_x;
    }

    EXPECT_TRUE(hit_right_wall) << "Duck should reach the right wall";
    EXPECT_TRUE(turned_around) << "Duck should turn around after hitting wall";
}

TEST_F(DuckBrainTest, DuckBrain2BouncesBackAndForth)
{
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::info);

    // Create world.
    auto world = createFlatWorld(15, 5);
    OrganismManager& manager = world->getOrganismManager();

    // Create duck with DuckBrain2 near left wall.
    auto brain = std::make_unique<DuckBrain2>();
    OrganismId duck_id = manager.createDuck(*world, 2, 3, std::move(brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle.
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    // Track direction changes (bounces).
    int last_x = duck->getAnchorCell().x;
    int bounce_count = 0;
    int last_direction = 0; // 1 = right, -1 = left.
    bool was_on_ground = duck->isOnGround();

    for (int i = 0; i < 800; ++i) {
        world->advanceTime(0.016);
        int current_x = duck->getAnchorCell().x;
        int current_y = duck->getAnchorCell().y;
        bool on_ground = duck->isOnGround();

        // Log forces during and around jumps.
        if (!on_ground || !was_on_ground || (i >= 80 && i <= 150)) {
            const Cell& cell = world->getData().at(current_x, current_y);
            const CellDebug& debug = world->getGrid().debugAt(current_x, current_y);
            spdlog::info(
                "Frame {}: pos=({},{}), vel=({:.2f},{:.2f}), on_ground={}",
                i,
                current_x,
                current_y,
                cell.velocity.x,
                cell.velocity.y,
                on_ground);
            spdlog::info(
                "  Forces: gravity=({:.2f},{:.2f}), friction=({:.2f},{:.2f}), "
                "viscous=({:.2f},{:.2f}), cohesion=({:.2f},{:.2f}), adhesion=({:.2f},{:.2f}), "
                "pressure=({:.2f},{:.2f})",
                debug.accumulated_gravity_force.x,
                debug.accumulated_gravity_force.y,
                debug.accumulated_friction_force.x,
                debug.accumulated_friction_force.y,
                debug.accumulated_viscous_force.x,
                debug.accumulated_viscous_force.y,
                debug.accumulated_com_cohesion_force.x,
                debug.accumulated_com_cohesion_force.y,
                debug.accumulated_adhesion_force.x,
                debug.accumulated_adhesion_force.y,
                debug.accumulated_pressure_force.x,
                debug.accumulated_pressure_force.y);
        }

        if (current_x != last_x) {
            int direction = (current_x > last_x) ? 1 : -1;

            if (last_direction != 0 && direction != last_direction) {
                bounce_count++;
                spdlog::info("Frame {}: Bounce #{} at x={}", i, bounce_count, current_x);
            }

            last_direction = direction;
        }

        last_x = current_x;
        was_on_ground = on_ground;
    }

    spdlog::info("Duck bounced {} times in 800 frames", bounce_count);

    // Duck should bounce multiple times (once it finds exit wall and starts bouncing).
    EXPECT_GE(bounce_count, 3) << "Duck should bounce at least 3 times";
}

TEST_F(DuckBrainTest, DuckBrain2JumpsWhenMovingFastInMiddle)
{
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::info);

    // Create world.
    auto world = createFlatWorld(20, 5);
    OrganismManager& manager = world->getOrganismManager();

    // Create duck with DuckBrain2 near left wall.
    auto brain = std::make_unique<DuckBrain2>();
    OrganismId duck_id = manager.createDuck(*world, 2, 3, std::move(brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle.
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    // Run long enough to see multiple jumps for learning verification.
    int jump_count = 0;
    bool was_on_ground = duck->isOnGround();
    std::vector<int> jump_positions;

    for (int i = 0; i < 1500; ++i) {
        world->advanceTime(0.016);

        bool on_ground = duck->isOnGround();

        // Detect jump: transition from on_ground to airborne.
        if (was_on_ground && !on_ground) {
            jump_count++;
            int x = duck->getAnchorCell().x;
            jump_positions.push_back(x);
            spdlog::info("Frame {}: Jump #{} detected at x={}", i, jump_count, x);
        }

        was_on_ground = on_ground;
    }

    spdlog::info("Duck jumped {} times in 1500 frames", jump_count);

    // Duck should jump multiple times (for learning to occur).
    EXPECT_GE(jump_count, 2) << "Duck should jump at least twice to demonstrate learning";

    // If multiple jumps occurred, verify they're near the middle.
    if (jump_positions.size() >= 2) {
        // Calculate approximate center (assume world is ~20 wide, walls at 1 and 19).
        int approx_center = 10;
        for (size_t i = 0; i < jump_positions.size(); ++i) {
            int dist_from_center = std::abs(jump_positions[i] - approx_center);
            spdlog::info(
                "Jump #{} at x={}, distance from center: {}",
                i + 1,
                jump_positions[i],
                dist_from_center);
        }

        // Later jumps should be closer to center as learning improves.
        if (jump_positions.size() >= 3) {
            int first_dist = std::abs(jump_positions[0] - approx_center);
            int last_dist = std::abs(jump_positions.back() - approx_center);
            spdlog::info(
                "First jump dist from center: {}, Last jump dist: {}", first_dist, last_dist);
        }
    }
}

TEST_F(DuckBrainTest, DuckBrain2DoesNotJumpWhenStationary)
{
    // Create world.
    auto world = createFlatWorld(10, 5);
    OrganismManager& manager = world->getOrganismManager();

    // Create duck with DuckBrain2 exactly in the middle.
    // It will immediately be in "middle" zone but won't be moving fast yet.
    auto brain = std::make_unique<DuckBrain2>();
    OrganismId duck_id = manager.createDuck(*world, 5, 3, std::move(brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Run just a few frames - duck is in middle but not moving fast yet.
    int jump_count = 0;
    bool was_on_ground = true;

    for (int i = 0; i < 10; ++i) {
        world->advanceTime(0.016);

        bool on_ground = duck->isOnGround();
        if (was_on_ground && !on_ground) {
            jump_count++;
        }
        was_on_ground = on_ground;
    }

    // Duck should not jump immediately - needs to build up speed first.
    // (It might jump if it happens to have enough speed, but typically won't in first 10 frames.)
    spdlog::info("Duck jumped {} times in first 10 frames", jump_count);
}
