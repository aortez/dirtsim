#include "core/LoggingChannels.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/Goose.h"
#include "core/organisms/GooseBrain.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace DirtSim;

/**
 * Test brain that allows explicit control of goose actions.
 */
class TestGooseBrain : public GooseBrain {
public:
    void think(Goose& goose, const GooseSensoryData& sensory, double deltaTime) override
    {
        (void)sensory;
        (void)deltaTime;

        switch (current_action_) {
        case GooseAction::RUN_LEFT:
            goose.setWalkDirection(-1.0f);
            break;
        case GooseAction::RUN_RIGHT:
            goose.setWalkDirection(1.0f);
            break;
        case GooseAction::JUMP:
            goose.jump();
            break;
        case GooseAction::WAIT:
        default:
            goose.setWalkDirection(0.0f);
            break;
        }
    }

    void setAction(GooseAction action) { current_action_ = action; }
};

class GooseTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::debug); }

    /**
     * Create a world with air and a WALL floor.
     *
     * Layout (20x10):
     *   Row 0: WALL border
     *   Row 1-8: AIR
     *   Row 9: WALL floor
     */
    std::unique_ptr<World> createTestWorld(uint32_t width = 20, uint32_t height = 10)
    {
        auto world = std::make_unique<World>(width, height);

        // Clear interior to air.
        for (uint32_t y = 1; y < height - 1; ++y) {
            for (uint32_t x = 1; x < width - 1; ++x) {
                world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
            }
        }

        // Ensure floor is WALL.
        for (uint32_t x = 0; x < width; ++x) {
            world->getData().at(x, height - 1).replaceMaterial(MaterialType::WALL, 1.0);
        }

        return world;
    }

    void printWorld(const World& world, const std::string& label)
    {
        spdlog::info("=== {} ===", label);
        const WorldData& data = world.getData();
        for (uint32_t y = 0; y < data.height; ++y) {
            std::string row;
            for (uint32_t x = 0; x < data.width; ++x) {
                const Cell& cell = data.at(x, y);
                if (cell.material_type == MaterialType::WALL) {
                    row += "W";
                }
                else if (cell.material_type == MaterialType::WOOD) {
                    row += "G"; // Goose cell.
                }
                else if (cell.material_type == MaterialType::AIR || cell.isEmpty()) {
                    row += ".";
                }
                else {
                    row += "?";
                }
            }
            spdlog::info("  {}", row);
        }
    }
};

// =============================================================================
// Basic Creation Tests
// =============================================================================

TEST_F(GooseTest, CreateGoosePlacesWoodCell)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create goose above the floor.
    OrganismId goose_id = manager.createGoose(*world, 10, 8);

    EXPECT_NE(goose_id, INVALID_ORGANISM_ID);

    Goose* goose = manager.getGoose(goose_id);
    ASSERT_NE(goose, nullptr);

    // Check that WOOD cell was placed.
    const Cell& cell = world->getData().at(10, 8);
    EXPECT_EQ(cell.material_type, MaterialType::WOOD);
    EXPECT_EQ(cell.organism_id, goose_id);

    // Check goose's anchor cell.
    EXPECT_EQ(goose->getAnchorCell(), Vector2i(10, 8));

    printWorld(*world, "After goose creation");
}

// =============================================================================
// Standing Still Tests
// =============================================================================

TEST_F(GooseTest, GooseStandsStillWithWaitAction)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create a test brain that just waits.
    auto test_brain = std::make_unique<TestGooseBrain>();
    test_brain->setAction(GooseAction::WAIT);

    // Create goose just above the floor (y=8, floor at y=9).
    OrganismId goose_id = manager.createGoose(*world, 10, 8, std::move(test_brain));
    Goose* goose = manager.getGoose(goose_id);
    ASSERT_NE(goose, nullptr);

    printWorld(*world, "Initial state - goose at (10, 8)");

    int start_x = goose->getAnchorCell().x;

    // Run physics for many frames.
    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);
    }

    printWorld(*world, "After 100 frames with WAIT action");

    int end_x = goose->getAnchorCell().x;

    // Goose should stay at same X position (not walking).
    EXPECT_EQ(start_x, end_x) << "Goose should not move horizontally when waiting";

    // Goose should be near the floor (y=8, since floor is at y=9).
    int end_y = goose->getAnchorCell().y;
    EXPECT_EQ(end_y, 8) << "Goose should be resting on floor at y=8";

    // Velocity should be near zero.
    EXPECT_NEAR(goose->velocity.x, 0.0, 0.1) << "Horizontal velocity should be near zero";
    EXPECT_NEAR(goose->velocity.y, 0.0, 0.5) << "Vertical velocity should be near zero when on ground";
}

TEST_F(GooseTest, GooseFallsToFloorThenStops)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create a test brain that just waits.
    auto test_brain = std::make_unique<TestGooseBrain>();
    test_brain->setAction(GooseAction::WAIT);

    // Create goose high up in the air (y=2).
    OrganismId goose_id = manager.createGoose(*world, 10, 2, std::move(test_brain));
    Goose* goose = manager.getGoose(goose_id);
    ASSERT_NE(goose, nullptr);

    printWorld(*world, "Initial state - goose at (10, 2)");

    // Run physics - goose should fall due to gravity.
    for (int frame = 0; frame < 200; ++frame) {
        world->advanceTime(0.016);
    }

    printWorld(*world, "After 200 frames - should have fallen to floor");

    // Goose should now be at floor level (y=8).
    int end_y = goose->getAnchorCell().y;
    EXPECT_EQ(end_y, 8) << "Goose should have fallen to rest on floor at y=8";

    // Should be on ground.
    EXPECT_TRUE(goose->isOnGround()) << "Goose should detect it is on ground";

    // Velocity should be near zero after settling.
    EXPECT_NEAR(goose->velocity.y, 0.0, 0.5) << "Vertical velocity should be near zero after landing";
}

// =============================================================================
// Walking Tests
// =============================================================================

TEST_F(GooseTest, GooseWalksRightWhenOnGround)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create a test brain we can control.
    auto test_brain = std::make_unique<TestGooseBrain>();
    TestGooseBrain* brain_ptr = test_brain.get();

    // Create goose on the floor (y=8, floor at y=9).
    int start_x = 5;
    int expected_y = 8; // Should stay just above the floor.
    OrganismId goose_id = manager.createGoose(*world, start_x, expected_y, std::move(test_brain));
    Goose* goose = manager.getGoose(goose_id);
    ASSERT_NE(goose, nullptr);

    // Let goose settle onto ground first.
    brain_ptr->setAction(GooseAction::WAIT);
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    ASSERT_TRUE(goose->isOnGround()) << "Goose should be on ground before walking test";
    ASSERT_EQ(goose->getAnchorCell().y, expected_y)
        << "Goose should be at y=" << expected_y << " after settling, not inside the floor";
    spdlog::info("Goose settled at ({}, {})", goose->getAnchorCell().x, goose->getAnchorCell().y);

    // Now walk right for 100 frames (~1.6 seconds).
    brain_ptr->setAction(GooseAction::RUN_RIGHT);
    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);
    }

    Vector2i final_pos = goose->getAnchorCell();
    int distance_moved = final_pos.x - start_x;

    spdlog::info("Goose walked from x={} to x={}, distance={} cells", start_x, final_pos.x, distance_moved);

    // Check horizontal movement: expect 2-6 cells in 100 frames at ~60fps.
    // With WALK_FORCE=50 and reasonable physics, this is approximately 2-3 cells/second.
    EXPECT_GE(distance_moved, 2) << "Goose should move at least 2 cells when walking right for 100 frames";
    EXPECT_LE(distance_moved, 8) << "Goose should not move more than 8 cells (unreasonably fast)";

    // Check vertical position: should still be on the floor, not fallen through.
    EXPECT_EQ(final_pos.y, expected_y)
        << "Goose should stay at y=" << expected_y << " while walking, not fall into floor";
}

TEST_F(GooseTest, GooseWalksLeftWhenOnGround)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create a test brain we can control.
    auto test_brain = std::make_unique<TestGooseBrain>();
    TestGooseBrain* brain_ptr = test_brain.get();

    // Create goose on the floor, starting from the right side (y=8, floor at y=9).
    int start_x = 15;
    int expected_y = 8; // Should stay just above the floor.
    OrganismId goose_id = manager.createGoose(*world, start_x, expected_y, std::move(test_brain));
    Goose* goose = manager.getGoose(goose_id);
    ASSERT_NE(goose, nullptr);

    // Let goose settle onto ground first.
    brain_ptr->setAction(GooseAction::WAIT);
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    ASSERT_TRUE(goose->isOnGround()) << "Goose should be on ground before walking test";
    ASSERT_EQ(goose->getAnchorCell().y, expected_y)
        << "Goose should be at y=" << expected_y << " after settling, not inside the floor";
    spdlog::info("Goose settled at ({}, {})", goose->getAnchorCell().x, goose->getAnchorCell().y);

    // Now walk left for 100 frames (~1.6 seconds).
    brain_ptr->setAction(GooseAction::RUN_LEFT);
    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);
    }

    Vector2i final_pos = goose->getAnchorCell();
    int distance_moved = start_x - final_pos.x; // Inverted for left movement.

    spdlog::info("Goose walked from x={} to x={}, distance={} cells left", start_x, final_pos.x, distance_moved);

    // Check horizontal movement: expect 2-6 cells in 100 frames at ~60fps.
    EXPECT_GE(distance_moved, 2) << "Goose should move at least 2 cells when walking left for 100 frames";
    EXPECT_LE(distance_moved, 8) << "Goose should not move more than 8 cells (unreasonably fast)";

    // Check vertical position: should still be on the floor, not fallen through.
    EXPECT_EQ(final_pos.y, expected_y)
        << "Goose should stay at y=" << expected_y << " while walking, not fall into floor";
}

TEST_F(GooseTest, GooseStopsWhenWalkDirectionChangesToZero)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create a test brain we can control.
    auto test_brain = std::make_unique<TestGooseBrain>();
    TestGooseBrain* brain_ptr = test_brain.get();

    // Create goose on the floor.
    OrganismId goose_id = manager.createGoose(*world, 10, 8, std::move(test_brain));
    Goose* goose = manager.getGoose(goose_id);
    ASSERT_NE(goose, nullptr);

    // Let goose settle.
    brain_ptr->setAction(GooseAction::WAIT);
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    // Walk right to build up velocity.
    brain_ptr->setAction(GooseAction::RUN_RIGHT);
    for (int i = 0; i < 30; ++i) {
        world->advanceTime(0.016);
    }

    // Goose should have some velocity now.
    double velocity_while_walking = goose->velocity.x;
    spdlog::info("Velocity while walking: {}", velocity_while_walking);
    EXPECT_GT(velocity_while_walking, 0.0) << "Goose should have positive velocity while walking right";

    // Now stop.
    brain_ptr->setAction(GooseAction::WAIT);
    int x_when_stopped = goose->getAnchorCell().x;

    // Run more frames - goose should slow down and stop.
    for (int i = 0; i < 50; ++i) {
        world->advanceTime(0.016);
    }

    int final_x = goose->getAnchorCell().x;
    int drift = final_x - x_when_stopped;

    spdlog::info("Goose drifted {} cells after stopping", drift);

    // Goose shouldn't drift too far after stopping (maybe 1-2 cells due to momentum).
    EXPECT_LE(drift, 3) << "Goose should not drift more than 3 cells after stopping";
}

// =============================================================================
// Collision Tests
// =============================================================================

TEST_F(GooseTest, GooseCannotWalkThroughVerticalWall)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Add a vertical wall in the middle of the world.
    // Wall at x=12, from y=1 to y=8.
    int wall_x = 12;
    for (int y = 1; y <= 8; ++y) {
        world->getData().at(wall_x, y).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // Create a test brain we can control.
    auto test_brain = std::make_unique<TestGooseBrain>();
    TestGooseBrain* brain_ptr = test_brain.get();

    // Create goose to the left of the wall (y=8, floor at y=9).
    int start_x = 5;
    int expected_y = 8;
    OrganismId goose_id = manager.createGoose(*world, start_x, expected_y, std::move(test_brain));
    Goose* goose = manager.getGoose(goose_id);
    ASSERT_NE(goose, nullptr);

    // Let goose settle.
    brain_ptr->setAction(GooseAction::WAIT);
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    ASSERT_EQ(goose->getAnchorCell().y, expected_y)
        << "Goose should settle at y=" << expected_y;

    printWorld(*world, "Before walking toward wall");

    // Walk right toward the wall for many frames.
    brain_ptr->setAction(GooseAction::RUN_RIGHT);
    for (int frame = 0; frame < 200; ++frame) {
        world->advanceTime(0.016);
    }

    printWorld(*world, "After walking toward wall");

    Vector2i final_pos = goose->getAnchorCell();
    spdlog::info("Goose ended at ({}, {}), wall at x={}", final_pos.x, final_pos.y, wall_x);

    // Goose should have stopped before the wall (at x=11 or less).
    EXPECT_LT(final_pos.x, wall_x)
        << "Goose should stop before the wall at x=" << wall_x << ", not pass through it";

    // Goose should be right next to the wall (at x=11).
    EXPECT_GE(final_pos.x, wall_x - 2)
        << "Goose should have walked up to the wall, ending near x=" << (wall_x - 1);

    // Goose should still be at correct y position.
    EXPECT_EQ(final_pos.y, expected_y)
        << "Goose should stay at y=" << expected_y << " while walking";
}

TEST_F(GooseTest, GooseCannotWalkThroughOtherOrganism)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create a stationary goose (the "obstacle").
    auto obstacle_brain = std::make_unique<TestGooseBrain>();
    obstacle_brain->setAction(GooseAction::WAIT);
    int obstacle_x = 12;
    int expected_y = 8;
    OrganismId obstacle_id = manager.createGoose(*world, obstacle_x, expected_y, std::move(obstacle_brain));
    Goose* obstacle_goose = manager.getGoose(obstacle_id);
    ASSERT_NE(obstacle_goose, nullptr);

    // Create a walking goose that will approach the obstacle.
    auto walker_brain = std::make_unique<TestGooseBrain>();
    TestGooseBrain* walker_brain_ptr = walker_brain.get();
    int start_x = 5;
    OrganismId walker_id = manager.createGoose(*world, start_x, expected_y, std::move(walker_brain));
    Goose* walker_goose = manager.getGoose(walker_id);
    ASSERT_NE(walker_goose, nullptr);

    // Let both geese settle.
    walker_brain_ptr->setAction(GooseAction::WAIT);
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    ASSERT_EQ(walker_goose->getAnchorCell().y, expected_y)
        << "Walker goose should settle at y=" << expected_y;

    printWorld(*world, "Before walking toward other goose");

    // Walk right toward the obstacle goose.
    walker_brain_ptr->setAction(GooseAction::RUN_RIGHT);
    for (int frame = 0; frame < 200; ++frame) {
        world->advanceTime(0.016);
    }

    printWorld(*world, "After walking toward other goose");

    Vector2i walker_pos = walker_goose->getAnchorCell();
    Vector2i obstacle_pos = obstacle_goose->getAnchorCell();
    spdlog::info("Walker ended at ({}, {}), obstacle at ({}, {})",
        walker_pos.x, walker_pos.y, obstacle_pos.x, obstacle_pos.y);

    // Walker should have stopped before the obstacle (not overlapping).
    EXPECT_LT(walker_pos.x, obstacle_pos.x)
        << "Walker goose should stop before the obstacle goose, not overlap";

    // Walker should be right next to the obstacle.
    EXPECT_GE(walker_pos.x, obstacle_pos.x - 2)
        << "Walker goose should have walked up to the obstacle";

    // Both geese should still be at correct y position.
    EXPECT_EQ(walker_pos.y, expected_y)
        << "Walker goose should stay at y=" << expected_y;
    EXPECT_EQ(obstacle_pos.y, expected_y)
        << "Obstacle goose should stay at y=" << expected_y;
}
