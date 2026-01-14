#include "CellTrackerUtil.h"
#include "core/GridOfCells.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/Goose.h"
#include "core/organisms/GooseBrain.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>
#include <iomanip>
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
     * Print goose physics state for debugging.
     */
    void printGooseState(int frame, const Goose* goose, const World& world)
    {
        Vector2i anchor = goose->getAnchorCell();
        const auto& data = world.getData();

        std::cout << std::setw(3) << frame << " | " << "pos=(" << std::fixed << std::setprecision(2)
                  << std::setw(6) << goose->position.x << "," << std::setw(5) << goose->position.y
                  << ") | " << "grid=(" << std::setw(2) << anchor.x << "," << anchor.y << ") | "
                  << "vel=(" << std::setw(6) << goose->velocity.x << "," << std::setw(6)
                  << goose->velocity.y << ") | " << "ground=" << (goose->isOnGround() ? "Y" : "N");

        // Print cell forces if valid position.
        if (anchor.x >= 0 && anchor.y >= 0 && anchor.x < data.width && anchor.y < data.height) {
            const Cell& cell = data.at(anchor.x, anchor.y);
            const auto& debug = world.getGrid().debugAt(anchor.x, anchor.y);
            std::cout << " | pend=(" << std::setw(5) << cell.pending_force.x << "," << std::setw(5)
                      << cell.pending_force.y << ")" << " grav=(" << std::setw(4)
                      << debug.accumulated_gravity_force.x << "," << std::setw(4)
                      << debug.accumulated_gravity_force.y << ")" << " fric=(" << std::setw(5)
                      << debug.accumulated_friction_force.x << "," << std::setw(5)
                      << debug.accumulated_friction_force.y << ")";
        }
        std::cout << "\n";
    }

    /**
     * Create a world with air and a WALL floor.
     *
     * Layout (20x10):
     *   Row 0: WALL border
     *   Row 1-8: AIR
     *   Row 9: WALL floor
     */
    std::unique_ptr<World> createTestWorld(int width = 20, int height = 10)
    {
        auto world = std::make_unique<World>(width, height);

        // Clear interior to air.
        for (int y = 1; y < height - 1; ++y) {
            for (int x = 1; x < width - 1; ++x) {
                world->getData().at(x, y).replaceMaterial(Material::EnumType::Air, 0.0);
            }
        }

        // Ensure floor is WALL.
        for (int x = 0; x < width; ++x) {
            world->getData().at(x, height - 1).replaceMaterial(Material::EnumType::Wall, 1.0);
        }

        return world;
    }

    void printWorld(const World& world, const std::string& label)
    {
        spdlog::info("=== {} ===", label);
        const WorldData& data = world.getData();
        for (int y = 0; y < data.height; ++y) {
            std::string row;
            for (int x = 0; x < data.width; ++x) {
                const Cell& cell = data.at(x, y);
                if (cell.material_type == Material::EnumType::Wall) {
                    row += "W";
                }
                else if (cell.material_type == Material::EnumType::Wood) {
                    row += "G"; // Goose cell.
                }
                else if (cell.material_type == Material::EnumType::Air || cell.isEmpty()) {
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
    EXPECT_EQ(cell.material_type, Material::EnumType::Wood);
    EXPECT_EQ(manager.at(Vector2i{ 10, 8 }), goose_id);

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

    // Set up cell tracker for detailed diagnostics.
    CellTracker tracker(*world, goose_id, 20);
    tracker.trackCell(goose->getAnchorCell(), Material::EnumType::Wood, 0);

    // Run physics for many frames.
    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(frame);

        // Track if the goose cell moved.
        Vector2i current_anchor = goose->getAnchorCell();
        if (current_anchor != Vector2i(10, 8)) {
            tracker.trackCell(current_anchor, Material::EnumType::Wood, frame);
        }
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
    EXPECT_NEAR(goose->velocity.y, 0.0, 0.5)
        << "Vertical velocity should be near zero when on ground";

    // Print detailed diagnostics only if test failed.
    if (HasFailure()) {
        std::cout << "\n=== GOOSE STANDING STILL DEBUG ===\n";
        tracker.printTableHeader();
        for (int frame = 0; frame < 100; frame += 10) {
            tracker.printTableRow(frame, true);
        }

        std::cout << "\n=== FINAL STATE ===\n";
        printGooseState(100, goose, *world);
        std::cout << "Goose position: (" << goose->position.x << ", " << goose->position.y << ")\n";
        std::cout << "Goose velocity: (" << goose->velocity.x << ", " << goose->velocity.y << ")\n";
        std::cout << "Goose on_ground: " << (goose->isOnGround() ? "true" : "false") << "\n";

        std::cout << "\n=== CELL FORCE HISTORY ===\n";
        tracker.printHistory(goose->getAnchorCell(), 100);
    }
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

    // Set up cell tracker.
    CellTracker tracker(*world, goose_id, 20);
    tracker.trackCell(goose->getAnchorCell(), Material::EnumType::Wood, 0);

    // Run physics - goose should fall due to gravity.
    for (int frame = 0; frame < 200; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(frame);

        // Track if the goose cell moved.
        Vector2i current_anchor = goose->getAnchorCell();
        if (frame < 100) {
            tracker.trackCell(current_anchor, Material::EnumType::Wood, frame);
        }
    }

    printWorld(*world, "After 200 frames - should have fallen to floor");

    // Goose should now be at floor level (y=8).
    int end_y = goose->getAnchorCell().y;
    EXPECT_EQ(end_y, 8) << "Goose should have fallen to rest on floor at y=8";

    // Should be on ground.
    EXPECT_TRUE(goose->isOnGround()) << "Goose should detect it is on ground";

    // Velocity should be near zero after settling.
    EXPECT_NEAR(goose->velocity.y, 0.0, 0.5)
        << "Vertical velocity should be near zero after landing";

    // Print detailed diagnostics only if test failed.
    if (HasFailure()) {
        std::cout << "\n=== GOOSE FALLING DEBUG ===\n";
        tracker.printTableHeader();
        for (int frame = 0; frame < 200; frame += 20) {
            tracker.printTableRow(frame, true);
        }

        std::cout << "\n=== FINAL STATE ===\n";
        std::cout << "Goose position: (" << goose->position.x << ", " << goose->position.y << ")\n";
        std::cout << "Goose velocity: (" << goose->velocity.x << ", " << goose->velocity.y << ")\n";
        std::cout << "Goose on_ground: " << (goose->isOnGround() ? "true" : "false") << "\n";

        std::cout << "\n=== CELL FORCE HISTORY ===\n";
        tracker.printHistory(goose->getAnchorCell(), 200);
    }
}

// =============================================================================
// Walking Tests
// =============================================================================

TEST_F(GooseTest, GooseWalksRightWhenOnGround)
{
    // Use a larger world so goose has room to reach terminal velocity.
    auto world = createTestWorld(100, 10);
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

    // Set up cell tracker.
    CellTracker tracker(*world, goose_id, 20);
    tracker.trackCell(goose->getAnchorCell(), Material::EnumType::Wood, 0);

    // Walk right for 100 frames (~1.6 seconds), tracking velocity.
    brain_ptr->setAction(GooseAction::RUN_RIGHT);
    double max_velocity = 0.0;

    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(frame);
        tracker.trackCell(goose->getAnchorCell(), Material::EnumType::Wood, frame);

        // Track max velocity.
        Vector2i pos = goose->getAnchorCell();
        if (pos.x >= 0 && pos.x < 100) {
            double vel = goose->velocity.x;
            if (vel > max_velocity) {
                max_velocity = vel;
            }
        }
    }

    Vector2i final_pos = goose->getAnchorCell();
    int distance_moved = final_pos.x - start_x;

    // Check horizontal movement with ground friction.
    EXPECT_GE(distance_moved, 20)
        << "Goose should move at least 20 cells when walking right for 100 frames";
    EXPECT_LE(distance_moved, 35) << "Goose should not move more than 35 cells in 100 frames";

    // Check terminal velocity with ground friction (25-35 cells/sec).
    EXPECT_GE(max_velocity, 25.0) << "Goose terminal velocity should be at least 25 cells/sec";
    EXPECT_LE(max_velocity, 35.0) << "Goose terminal velocity should not exceed 35 cells/sec";

    // Check vertical position: should still be on the floor, not fallen through.
    EXPECT_EQ(final_pos.y, expected_y)
        << "Goose should stay at y=" << expected_y << " while walking, not fall into floor";

    // Print detailed diagnostics only if test failed.
    if (HasFailure()) {
        std::cout << "\n=== GOOSE WALKING RIGHT DEBUG ===\n";
        std::cout << "Walked from x=" << start_x << " to x=" << final_pos.x
                  << ", distance=" << distance_moved << " cells, max_velocity=" << max_velocity
                  << "\n";

        tracker.printTableHeader();
        for (int frame = 0; frame < 100; frame += 10) {
            tracker.printTableRow(frame, true);
        }

        std::cout << "\n=== FINAL STATE ===\n";
        printGooseState(100, goose, *world);

        std::cout << "\n=== CELL FORCE HISTORY (final position) ===\n";
        tracker.printHistory(goose->getAnchorCell(), 100);
    }
}

TEST_F(GooseTest, GooseWalksLeftWhenOnGround)
{
    // Use a larger world so goose has room to reach terminal velocity.
    auto world = createTestWorld(100, 10);
    OrganismManager& manager = world->getOrganismManager();

    // Create a test brain we can control.
    auto test_brain = std::make_unique<TestGooseBrain>();
    TestGooseBrain* brain_ptr = test_brain.get();

    // Create goose on the floor, starting from the right side (y=8, floor at y=9).
    int start_x = 90;
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

    // Set up cell tracker.
    CellTracker tracker(*world, goose_id, 20);
    tracker.trackCell(goose->getAnchorCell(), Material::EnumType::Wood, 0);

    // Walk left for 100 frames (~1.6 seconds), tracking velocity.
    brain_ptr->setAction(GooseAction::RUN_LEFT);
    double max_velocity = 0.0;
    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(frame);
        tracker.trackCell(goose->getAnchorCell(), Material::EnumType::Wood, frame);

        // Track max velocity (absolute value since going left).
        Vector2i pos = goose->getAnchorCell();
        if (pos.x >= 0 && pos.x < 100) {
            double vel = std::abs(goose->velocity.x);
            if (vel > max_velocity) {
                max_velocity = vel;
            }
        }
    }

    Vector2i final_pos = goose->getAnchorCell();
    int distance_moved = start_x - final_pos.x; // Inverted for left movement.

    // Check horizontal movement with ground friction.
    EXPECT_GE(distance_moved, 20)
        << "Goose should move at least 20 cells when walking left for 100 frames";
    EXPECT_LE(distance_moved, 35) << "Goose should not move more than 35 cells in 100 frames";

    // Check terminal velocity with ground friction (25-35 cells/sec).
    EXPECT_GE(max_velocity, 25.0) << "Goose terminal velocity should be at least 25 cells/sec";
    EXPECT_LE(max_velocity, 35.0) << "Goose terminal velocity should not exceed 35 cells/sec";

    // Check vertical position: should still be on the floor, not fallen through.
    EXPECT_EQ(final_pos.y, expected_y)
        << "Goose should stay at y=" << expected_y << " while walking, not fall into floor";

    // Print detailed diagnostics only if test failed.
    if (HasFailure()) {
        std::cout << "\n=== GOOSE WALKING LEFT DEBUG ===\n";
        std::cout << "Walked from x=" << start_x << " to x=" << final_pos.x
                  << ", distance=" << distance_moved << " cells, max_velocity=" << max_velocity
                  << "\n";

        tracker.printTableHeader();
        for (int frame = 0; frame < 100; frame += 10) {
            tracker.printTableRow(frame, true);
        }

        std::cout << "\n=== FINAL STATE ===\n";
        printGooseState(100, goose, *world);

        std::cout << "\n=== CELL FORCE HISTORY (final position) ===\n";
        tracker.printHistory(goose->getAnchorCell(), 100);
    }
}

TEST_F(GooseTest, GooseStopsWhenWalkDirectionChangesToZero)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create a test brain we can control.
    auto test_brain = std::make_unique<TestGooseBrain>();
    TestGooseBrain* brain_ptr = test_brain.get();

    // Create goose on the floor near the left side.
    OrganismId goose_id = manager.createGoose(*world, 2, 8, std::move(test_brain));
    Goose* goose = manager.getGoose(goose_id);
    ASSERT_NE(goose, nullptr);

    // Set up tracker.
    CellTracker tracker(*world, goose_id, 100);
    tracker.trackCell(goose->getAnchorCell(), Material::EnumType::Wood, 0);

    int frame = 0;

    // Let goose settle.
    brain_ptr->setAction(GooseAction::WAIT);
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
        tracker.recordFrame(++frame);
    }

    // Walk right until 1/3 of the way across the world.
    brain_ptr->setAction(GooseAction::RUN_RIGHT);
    int stop_x = static_cast<int>(world->getData().width) / 3;
    while (goose->getAnchorCell().x < stop_x) {
        world->advanceTime(0.016);
        ++frame;
        tracker.trackCell(goose->getAnchorCell(), Material::EnumType::Wood, frame);
        tracker.recordFrame(frame);
    }

    // Goose should have some velocity now.
    double velocity_while_walking = goose->velocity.x;
    EXPECT_GT(velocity_while_walking, 0.0)
        << "Goose should have positive velocity while walking right";

    // Now stop.
    brain_ptr->setAction(GooseAction::WAIT);
    int x_when_stopped = goose->getAnchorCell().x;
    double vel_when_stopped = goose->velocity.x;

    // Run more frames - goose should slow down and stop.
    for (int i = 0; i < 50; ++i) {
        world->advanceTime(0.016);
        ++frame;
        tracker.trackCell(goose->getAnchorCell(), Material::EnumType::Wood, frame);
        tracker.recordFrame(frame);
    }

    int final_x = goose->getAnchorCell().x;
    int drift = final_x - x_when_stopped;

    // Goose shouldn't drift too far after stopping (friction decelerates over time).
    EXPECT_LE(drift, 7) << "Goose should not drift more than 7 cells after stopping";

    // Print detailed diagnostics only if test failed.
    if (HasFailure()) {
        std::cout << "\n=== GOOSE STOPPING DEBUG ===\n";
        std::cout << "Velocity while walking: " << velocity_while_walking << "\n";
        std::cout << "Position when stopped: x=" << x_when_stopped
                  << ", velocity=" << vel_when_stopped << "\n";
        std::cout << "Final position: x=" << final_x << ", drift=" << drift << " cells\n";

        tracker.printTableHeader();
        for (int f = 0; f < frame; f += 10) {
            tracker.printTableRow(f, true);
        }

        std::cout << "\n=== CELL FORCE HISTORY (final position) ===\n";
        tracker.printHistory(goose->getAnchorCell(), frame);
    }
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
        world->getData().at(wall_x, y).replaceMaterial(Material::EnumType::Wall, 1.0);
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

    ASSERT_EQ(goose->getAnchorCell().y, expected_y) << "Goose should settle at y=" << expected_y;

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
    OrganismId obstacle_id =
        manager.createGoose(*world, obstacle_x, expected_y, std::move(obstacle_brain));
    Goose* obstacle_goose = manager.getGoose(obstacle_id);
    ASSERT_NE(obstacle_goose, nullptr);

    // Create a walking goose that will approach the obstacle.
    auto walker_brain = std::make_unique<TestGooseBrain>();
    TestGooseBrain* walker_brain_ptr = walker_brain.get();
    int start_x = 5;
    OrganismId walker_id =
        manager.createGoose(*world, start_x, expected_y, std::move(walker_brain));
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
    spdlog::info(
        "Walker ended at ({}, {}), obstacle at ({}, {})",
        walker_pos.x,
        walker_pos.y,
        obstacle_pos.x,
        obstacle_pos.y);

    // Walker should have stopped before the obstacle (not overlapping).
    EXPECT_LT(walker_pos.x, obstacle_pos.x)
        << "Walker goose should stop before the obstacle goose, not overlap";

    // Walker should be right next to the obstacle.
    EXPECT_GE(walker_pos.x, obstacle_pos.x - 2)
        << "Walker goose should have walked up to the obstacle";

    // Both geese should still be at correct y position.
    EXPECT_EQ(walker_pos.y, expected_y) << "Walker goose should stay at y=" << expected_y;
    EXPECT_EQ(obstacle_pos.y, expected_y) << "Obstacle goose should stay at y=" << expected_y;
}
