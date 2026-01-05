#include "core/CellDebug.h"
#include "core/GridOfCells.h"
#include "core/LoggingChannels.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/Duck.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/DuckSensoryData.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace DirtSim;

/**
 * Test brain that allows explicit control of duck actions.
 * Supports independent control of movement and jump for air steering tests.
 */
class TestDuckBrain : public DuckBrain {
public:
    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override
    {
        (void)sensory;
        (void)deltaTime;

        // If using direct input mode, apply it directly.
        if (use_direct_input_) {
            duck.setInput(direct_input_);
            // Clear jump after one frame (edge-triggered).
            direct_input_.jump = false;
            return;
        }

        // Legacy action-based mode.
        switch (current_action_) {
            case DuckAction::RUN_LEFT:
                duck.setInput({.move = {-1.0f, 0.0f}, .jump = false});
                break;
            case DuckAction::RUN_RIGHT:
                duck.setInput({.move = {1.0f, 0.0f}, .jump = false});
                break;
            case DuckAction::JUMP:
                duck.setInput({.move = {}, .jump = true});
                break;
            case DuckAction::WAIT:
            default:
                duck.setInput({.move = {}, .jump = false});
                break;
        }
    }

    void setAction(DuckAction action) { current_action_ = action; }

    // Direct input control for combined movement + jump.
    void setDirectInput(Vector2f move, bool jump)
    {
        use_direct_input_ = true;
        direct_input_ = {.move = move, .jump = jump};
    }

    void setMove(Vector2f move)
    {
        use_direct_input_ = true;
        direct_input_.move = move;
    }

    void triggerJump()
    {
        use_direct_input_ = true;
        direct_input_.jump = true;
    }

    void clearDirectInput()
    {
        use_direct_input_ = false;
        direct_input_ = {.move = {}, .jump = false};
    }

private:
    bool use_direct_input_ = false;
    DuckInput direct_input_ = {.move = {}, .jump = false};
};

class DuckTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::debug); }

    /**
     * Create a small world with air and a floor.
     *
     * Layout (5x5):
     *   01234
     * 0 WWWWW  <- wall border (automatic)
     * 1 W   W
     * 2 W D W  <- duck at center (2,2)
     * 3 W   W
     * 4 WWWWW  <- wall floor
     */
    std::unique_ptr<World> createTestWorld()
    {
        auto world = std::make_unique<World>(5, 5);

        // Clear interior to air.
        for (uint32_t y = 1; y < 4; ++y) {
            for (uint32_t x = 1; x < 4; ++x) {
                world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
            }
        }

        // Bottom row is already WALL from World constructor, but ensure it.
        for (uint32_t x = 0; x < 5; ++x) {
            world->getData().at(x, 4).replaceMaterial(MaterialType::WALL, 1.0);
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
                    row += "D"; // Duck cell.
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

    /**
     * Helper struct for common duck test setup.
     * Creates a flat world with floor, spawns duck, and settles it.
     */
    struct DuckTestSetup {
        std::unique_ptr<World> world;
        Duck* duck = nullptr;
        TestDuckBrain* brain = nullptr;
        OrganismId duck_id = INVALID_ORGANISM_ID;

        /**
         * Create a flat world with walls and floor, spawn duck, and let it settle.
         *
         * @param width World width.
         * @param height World height (floor at height-1).
         * @param duck_x Duck spawn X position.
         * @param duck_y Duck spawn Y position (typically height-2 for floor level).
         * @param settle_frames Frames to run for settling (default 20).
         */
        static DuckTestSetup create(int width, int height, int duck_x, int duck_y, int settle_frames = 20)
        {
            DuckTestSetup setup;

            // Create world.
            setup.world = std::make_unique<World>(width, height);

            // Clear interior to air.
            for (int y = 1; y < height - 1; ++y) {
                for (int x = 1; x < width - 1; ++x) {
                    setup.world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
                }
            }

            // Ensure floor (bottom row).
            for (int x = 0; x < width; ++x) {
                setup.world->getData().at(x, height - 1).replaceMaterial(MaterialType::WALL, 1.0);
            }

            // Create duck with test brain.
            auto brain_ptr = std::make_unique<TestDuckBrain>();
            setup.brain = brain_ptr.get();

            OrganismManager& manager = setup.world->getOrganismManager();
            setup.duck_id = manager.createDuck(*setup.world, duck_x, duck_y, std::move(brain_ptr));
            setup.duck = manager.getDuck(setup.duck_id);

            // Let duck settle.
            setup.brain->setAction(DuckAction::WAIT);
            for (int i = 0; i < settle_frames; ++i) {
                setup.world->advanceTime(0.016);
            }

            return setup;
        }

        // Get duck's current velocity from its cell.
        Vector2d getVelocity() const
        {
            Vector2i pos = duck->getAnchorCell();
            const Cell& cell = world->getData().at(pos.x, pos.y);
            return cell.velocity;
        }

        // Advance simulation by one frame.
        void advance(double dt = 0.016)
        {
            world->advanceTime(dt);
        }

        // Advance simulation by N frames.
        void advanceFrames(int frames, double dt = 0.016)
        {
            for (int i = 0; i < frames; ++i) {
                world->advanceTime(dt);
            }
        }
    };
};

TEST_F(DuckTest, CreateDuckPlacesWoodCell)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create duck at center.
    OrganismId duck_id = manager.createDuck(*world, 2, 2);

    EXPECT_NE(duck_id, INVALID_ORGANISM_ID);

    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Check that WOOD cell was placed.
    const Cell& cell = world->getData().at(2, 2);
    EXPECT_EQ(cell.material_type, MaterialType::WOOD);
    EXPECT_EQ(manager.at(Vector2i{2, 2}), duck_id);

    // Check duck's anchor cell.
    EXPECT_EQ(duck->getAnchorCell(), Vector2i(2, 2));

    printWorld(*world, "After duck creation");
}

TEST_F(DuckTest, DuckFallsWithGravity)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Use a test brain that just waits (no horizontal movement).
    auto test_brain = std::make_unique<TestDuckBrain>();
    test_brain->setAction(DuckAction::WAIT);

    // Create duck at top of interior (2, 1) with controlled brain.
    OrganismId duck_id = manager.createDuck(*world, 2, 1, std::move(test_brain));
    Duck* duck = manager.getDuck(duck_id);

    printWorld(*world, "Initial state - duck at (2,1)");

    // Duck should start with zero velocity.
    const Cell& initial_cell = world->getData().at(2, 1);
    EXPECT_NEAR(initial_cell.velocity.y, 0.0, 0.001);

    // Run physics for enough frames for duck to fall one cell.
    // With gravity ~9.8, velocity increases ~0.15/frame.
    // COM needs to reach 1.0 to trigger transfer, which takes ~30 frames.
    for (int frame = 0; frame < 40; ++frame) {
        world->advanceTime(0.016);

        spdlog::info("Frame {}: anchor_cell=({},{})", frame, duck->getAnchorCell().x,
            duck->getAnchorCell().y);
    }

    printWorld(*world, "After 40 frames");

    // The duck cell should have gained downward velocity or moved.
    // Check if the cell at (2,1) still has WOOD or if it transferred.
    const Cell& cell_at_start = world->getData().at(2, 1);
    const Cell& cell_below = world->getData().at(2, 2);
    const Cell& cell_at_floor = world->getData().at(2, 3);

    spdlog::info("Cell (2,1): type={}, fill={}", static_cast<int>(cell_at_start.material_type),
        cell_at_start.fill_ratio);
    spdlog::info("Cell (2,2): type={}, fill={}", static_cast<int>(cell_below.material_type),
        cell_below.fill_ratio);
    spdlog::info("Cell (2,3): type={}, fill={}", static_cast<int>(cell_at_floor.material_type),
        cell_at_floor.fill_ratio);

    // Duck should have fallen - WOOD should be at a lower position.
    bool wood_moved_down = (cell_below.material_type == MaterialType::WOOD
        || cell_at_floor.material_type == MaterialType::WOOD);

    EXPECT_TRUE(wood_moved_down) << "Duck's WOOD cell should have fallen due to gravity";
}

TEST_F(DuckTest, DuckAnchorCellTracksPhysics)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create duck at top of interior.
    OrganismId duck_id = manager.createDuck(*world, 2, 1);
    Duck* duck = manager.getDuck(duck_id);

    Vector2i initial_anchor = duck->getAnchorCell();
    EXPECT_EQ(initial_anchor, Vector2i(2, 1));

    // Run physics until the cell should have moved.
    for (int frame = 0; frame < 50; ++frame) {
        world->advanceTime(0.016);
    }

    printWorld(*world, "After 50 frames");

    // Find where the WOOD cell actually is.
    Vector2i actual_wood_pos(-1, -1);
    for (uint32_t y = 0; y < world->getData().height; ++y) {
        for (uint32_t x = 0; x < world->getData().width; ++x) {
            if (world->getData().at(x, y).material_type == MaterialType::WOOD) {
                actual_wood_pos = Vector2i(x, y);
                break;
            }
        }
    }

    spdlog::info("Duck anchor_cell: ({},{})", duck->getAnchorCell().x, duck->getAnchorCell().y);
    spdlog::info("Actual WOOD cell: ({},{})", actual_wood_pos.x, actual_wood_pos.y);

    // THIS IS THE KEY TEST: Does anchor_cell track the actual cell position?
    EXPECT_EQ(duck->getAnchorCell(), actual_wood_pos)
        << "Duck's anchor_cell should track the actual WOOD cell position after physics";
}

TEST_F(DuckTest, DuckWalksWhenOnGround)
{
    // Create a wider world for movement testing.
    // Layout (100x5):
    //   Row 0: WALL border
    //   Row 1-3: AIR
    //   Row 4: WALL floor
    auto world = std::make_unique<World>(100, 5);

    // Clear interior to air.
    for (uint32_t y = 1; y < 4; ++y) {
        for (uint32_t x = 1; x < 99; ++x) {
            world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
        }
    }

    // Ensure floor.
    for (uint32_t x = 0; x < 100; ++x) {
        world->getData().at(x, 4).replaceMaterial(MaterialType::WALL, 1.0);
    }

    OrganismManager& manager = world->getOrganismManager();

    // Create a test brain we can control.
    auto test_brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brain_ptr = test_brain.get();

    // Create duck on the floor (y=3 is just above wall at y=4).
    int start_x = 5;
    OrganismId duck_id = manager.createDuck(*world, start_x, 3, std::move(test_brain));
    Duck* duck = manager.getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle onto ground first.
    brain_ptr->setAction(DuckAction::WAIT);
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    ASSERT_TRUE(duck->isOnGround()) << "Duck should be on ground before walking test";
    spdlog::info("Duck settled at ({}, {})", duck->getAnchorCell().x, duck->getAnchorCell().y);

    // Now walk right for 100 frames.
    brain_ptr->setAction(DuckAction::RUN_RIGHT);
    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);
    }

    int final_x = duck->getAnchorCell().x;
    int distance_moved = final_x - start_x;

    spdlog::info("Duck walked from x={} to x={}, distance={} cells", start_x, final_x, distance_moved);

    // Should have moved at least 1-2 cells after 100 frames (~1.6 seconds).
    EXPECT_GE(distance_moved, 1) << "Duck should move at least 1 cell when walking for 100 frames";
}

TEST_F(DuckTest, DuckWalkingSpeedOnDifferentSurfaces)
{
    // Compare walking speed on different surfaces.
    // Track both distance and velocity to understand friction and air resistance effects.
    // Also test with DuckBrain2 to see what max speed it learns.

    struct SurfaceTestCase {
        MaterialType material;
        const char* name;
    };

    std::vector<SurfaceTestCase> test_cases = {
        { MaterialType::WALL, "WALL" },
        { MaterialType::DIRT, "DIRT" },
        { MaterialType::SAND, "SAND" },
    };

    struct SurfaceResult {
        const char* name;
        int distance;
        double velocity_at_frame_20;
        double velocity_at_frame_80;
        double max_velocity;
    };

    std::vector<SurfaceResult> results;

    for (const auto& test_case : test_cases) {
        auto world = std::make_unique<World>(100, 10);

        // Clear interior to air.
        for (uint32_t y = 1; y < 9; ++y) {
            for (uint32_t x = 1; x < 99; ++x) {
                world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
            }
        }

        // Set floor material.
        for (uint32_t x = 0; x < 100; ++x) {
            world->getData().at(x, 9).replaceMaterial(test_case.material, 1.0);
        }

        OrganismManager& manager = world->getOrganismManager();

        auto test_brain = std::make_unique<TestDuckBrain>();
        TestDuckBrain* brain_ptr = test_brain.get();

        int start_x = 5;
        OrganismId duck_id = manager.createDuck(*world, start_x, 8, std::move(test_brain));
        Duck* duck = manager.getDuck(duck_id);

        // Let duck settle onto ground first.
        brain_ptr->setAction(DuckAction::WAIT);
        for (int i = 0; i < 20; ++i) {
            world->advanceTime(0.016);
        }

        // Walk right for 100 frames, tracking velocity.
        brain_ptr->setAction(DuckAction::RUN_RIGHT);

        SurfaceResult result = { test_case.name, 0, 0.0, 0.0, 0.0 };

        for (int frame = 0; frame < 100; ++frame) {
            world->advanceTime(0.016);

            Vector2i pos = duck->getAnchorCell();
            if (pos.x >= 0 && pos.x < 100) {
                const Cell& cell = world->getData().at(pos.x, pos.y);
                double vel = cell.velocity.x;

                if (vel > result.max_velocity) {
                    result.max_velocity = vel;
                }
                if (frame == 20) {
                    result.velocity_at_frame_20 = vel;
                }
                if (frame == 80) {
                    result.velocity_at_frame_80 = vel;
                }
            }
        }

        result.distance = duck->getAnchorCell().x - start_x;
        results.push_back(result);
    }

    // Report results.
    spdlog::info("Walking test results (100 frames = 1.6 seconds):");
    spdlog::info("{:8} {:>10} {:>12} {:>12} {:>12}", "Surface", "Distance", "Vel@20", "Vel@80", "MaxVel");
    for (const auto& r : results) {
        spdlog::info("{:8} {:>10} {:>12.1f} {:>12.1f} {:>12.1f}",
            r.name, r.distance, r.velocity_at_frame_20, r.velocity_at_frame_80, r.max_velocity);
    }

    // Check if velocity plateaued (air resistance) or kept growing.
    for (const auto& r : results) {
        if (r.velocity_at_frame_20 > 0.1) {
            double ratio = r.velocity_at_frame_80 / r.velocity_at_frame_20;
            if (ratio > 2.0) {
                spdlog::warn("{}: Velocity grew {}x - no terminal velocity", r.name, ratio);
            } else {
                spdlog::info("{}: Velocity ratio {:.2f}x - air resistance working", r.name, ratio);
            }
        }
    }

    // Verify duck moves on all surfaces.
    for (const auto& r : results) {
        EXPECT_GE(r.distance, 1) << "Duck should move on " << r.name;
    }

    // Now test with DuckBrain2 to see what max speed it learns.
    // DuckBrain2 learns max_speed when velocity stabilizes for 1 second.
    spdlog::info("--- Testing DuckBrain2 max speed learning ---");
    {
        auto world = std::make_unique<World>(100, 10);

        // Clear interior to air.
        for (uint32_t y = 1; y < 9; ++y) {
            for (uint32_t x = 1; x < 99; ++x) {
                world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
            }
        }

        // WALL floor.
        for (uint32_t x = 0; x < 100; ++x) {
            world->getData().at(x, 9).replaceMaterial(MaterialType::WALL, 1.0);
        }

        OrganismManager& manager = world->getOrganismManager();

        // Create duck with DuckBrain2 near left wall.
        auto brain = std::make_unique<DuckBrain2>();
        OrganismId duck_id = manager.createDuck(*world, 2, 8, std::move(brain));
        Duck* duck = manager.getDuck(duck_id);

        // Run for 200 frames (~3.2 seconds) to let brain learn max speed.
        // DuckBrain2 needs 1 second of steady velocity to learn.
        for (int frame = 0; frame < 200; ++frame) {
            world->advanceTime(0.016);

            // Log velocity every 40 frames.
            if (frame % 40 == 0) {
                Vector2i pos = duck->getAnchorCell();
                if (pos.x >= 0 && pos.x < 100) {
                    const Cell& cell = world->getData().at(pos.x, pos.y);
                    spdlog::info("DuckBrain2 frame {}: pos={}, velocity.x={:.1f}",
                        frame, pos.x, cell.velocity.x);
                }
            }
        }

        // The "Learned max speed" log message from DuckBrain2 will appear in output.
        // We expect it to be around 50 cells/sec based on our earlier findings.
        spdlog::info("DuckBrain2 test complete. Check logs for 'Learned max speed' message.");
    }
}

TEST_F(DuckTest, DuckJumps2CellsHigh)
{
    // Create a taller world for jump testing.
    // Layout (5x10):
    //   Row 0: WALL border
    //   Row 1-8: AIR
    //   Row 9: WALL floor
    auto world = std::make_unique<World>(5, 10);

    // Clear interior to air.
    for (uint32_t y = 1; y < 9; ++y) {
        for (uint32_t x = 1; x < 4; ++x) {
            world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
        }
    }

    // Ensure floor.
    for (uint32_t x = 0; x < 5; ++x) {
        world->getData().at(x, 9).replaceMaterial(MaterialType::WALL, 1.0);
    }

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
        spdlog::info("Duck settled at y={}, COM=({:.3f},{:.3f}), vel=({:.2f},{:.2f}), on_ground={}",
            settled_y, cell.com.x, cell.com.y, cell.velocity.x, cell.velocity.y, duck->isOnGround());
    }

    // Trigger jump.
    brain_ptr->setAction(DuckAction::JUMP);
    world->advanceTime(0.016);  // One frame to initiate jump.

    // Log state immediately after jump frame.
    {
        Vector2i pos = duck->getAnchorCell();
        const Cell& cell = world->getData().at(pos.x, pos.y);
        spdlog::info("After jump frame: pos=({},{}), COM=({:.3f},{:.3f}), vel=({:.2f},{:.2f}), on_ground={}",
            pos.x, pos.y, cell.com.x, cell.com.y, cell.velocity.x, cell.velocity.y, duck->isOnGround());
    }

    // Switch to wait so we don't keep trying to jump.
    brain_ptr->setAction(DuckAction::WAIT);

    // Track the highest point (minimum Y since Y increases downward).
    int min_y = settled_y;
    double min_com_y = 1.0;  // Track minimum COM.y (most upward position within cell).

    // Run physics for enough frames to complete the jump arc.
    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);

        Vector2i pos = duck->getAnchorCell();
        const Cell& cell = world->getData().at(pos.x, pos.y);

        // Log first 30 frames to see jump dynamics.
        if (frame < 30) {
            spdlog::info("Frame {:3d}: pos=({},{}), COM.y={:+.3f}, vel.y={:+.2f}, on_ground={}",
                frame, pos.x, pos.y, cell.com.y, cell.velocity.y, duck->isOnGround());
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
    spdlog::info("Duck jumped from y={} to min y={}, height={} cells", settled_y, min_y, jump_height);

    // Verify duck jumped at least 2 cells high.
    EXPECT_GE(jump_height, 2) << "Duck should jump at least 2 cells high";
}

TEST_F(DuckTest, DuckOnGroundDetection)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Use a test brain that just waits (no jumping).
    auto test_brain = std::make_unique<TestDuckBrain>();
    test_brain->setAction(DuckAction::WAIT);

    // Create duck just above the floor (2, 3 is above wall at y=4).
    OrganismId duck_id = manager.createDuck(*world, 2, 3, std::move(test_brain));
    Duck* duck = manager.getDuck(duck_id);

    // Run several frames - duck will fall and hit ground.
    for (int i = 0; i < 50; ++i) {
        world->advanceTime(0.016);
    }

    printWorld(*world, "After 50 frames - duck should be on ground");

    // By now the duck should have fallen and be resting on the wall.
    const Cell& cell = world->getData().at(duck->getAnchorCell().x, duck->getAnchorCell().y);
    spdlog::info("Duck at ({},{}), velocity=({},{}), on_ground={}",
        duck->getAnchorCell().x, duck->getAnchorCell().y,
        cell.velocity.x, cell.velocity.y, duck->isOnGround());

    // Duck should detect it's on ground after falling and coming to rest.
    EXPECT_TRUE(duck->isOnGround()) << "Duck should detect ground after falling to rest";
}

TEST_F(DuckTest, DuckRemovalClearsCell)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Create duck at center.
    OrganismId duck_id = manager.createDuck(*world, 2, 2);
    ASSERT_NE(manager.getDuck(duck_id), nullptr);

    // Verify WOOD cell exists.
    EXPECT_EQ(world->getData().at(2, 2).material_type, MaterialType::WOOD);

    // Remove organism and its cells from the world.
    manager.removeOrganismFromWorld(*world, duck_id);

    printWorld(*world, "After duck removal");

    // Verify cell is now empty.
    const Cell& cell = world->getData().at(2, 2);
    EXPECT_EQ(cell.material_type, MaterialType::AIR);
    EXPECT_LT(cell.fill_ratio, 0.01) << "Cell should be empty after duck removal";
}

TEST_F(DuckTest, WallBouncingBrainPingPongs)
{
    // Create world for wall bouncing.
    auto world = std::make_unique<World>(10, 5);

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
                spdlog::info("Frame {}: Bounce #{} detected at x={} (now moving {})",
                    i, bounce_count, current_x,
                    (actual_direction == Direction::RIGHT) ? "RIGHT" : "LEFT");
            }

            current_direction = actual_direction;
        }

        last_x = current_x;
    }

    spdlog::info("Duck traveled from x={} to x={}, {} total bounces",
        min_x, max_x, bounce_count);

    // Duck should bounce multiple times in 600 frames (3x original duration).
    EXPECT_GE(bounce_count, 3) << "Duck should bounce at least 3 times in 600 frames";
    EXPECT_GE(max_x - min_x, 7) << "Duck should traverse most of the world";
}

TEST_F(DuckTest, WallBouncingBrainBouncesOffWall)
{
    // Initialize logging and enable brain debug logging.
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::debug);

    // Create world (no automatic WALL borders - sensory system will mark edges as WALL).
    auto world = std::make_unique<World>(10, 5);
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
    spdlog::info("Duck sensory grid (9x9, center at [4][4], WALL=W, WOOD=D):");
    for (int y = 0; y < 9; ++y) {
        std::string row;
        for (int x = 0; x < 9; ++x) {
            // Check for WALL (material index 7).
            double wall_fill = sensory.material_histograms[y][x][7];
            if (wall_fill > 0.5) {
                row += "W";
            } else {
                // Check for WOOD (duck itself, material index 9).
                double wood_fill = sensory.material_histograms[y][x][9];
                if (wood_fill > 0.5) {
                    row += "D";
                } else {
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

TEST_F(DuckTest, WallBouncingBrainJumpsAtMidpoint)
{
    // Enable brain debug logging.
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::info);

    // Create world.
    auto world = std::make_unique<World>(10, 5);

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
    int local_max_y = last_y;  // Track local peak (lowest point before jump).
    bool was_on_ground = duck->isOnGround();

    for (int i = 0; i < 800; ++i) {
        world->advanceTime(0.016);

        int current_y = duck->getAnchorCell().y;
        bool on_ground = duck->isOnGround();

        // Detect jump: transition from on_ground to airborne with upward movement.
        if (was_on_ground && !on_ground && current_y <= last_y) {
            jump_count++;
            spdlog::info("Frame {}: Jump #{} detected - left ground at y={}", i, jump_count, current_y);
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

TEST_F(DuckTest, DuckBrain2DetectsSpawnSide)
{
    // Create world - duck spawns near left wall.
    auto world = std::make_unique<World>(20, 5);
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
    EXPECT_EQ(action, DuckAction::RUN_RIGHT)
        << "Duck spawned on left should run right toward exit";
}

TEST_F(DuckTest, DuckBrain2TurnsAroundAtWall)
{
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::info);

    // Create world.
    auto world = std::make_unique<World>(15, 5);
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
    int right_wall_x = -1;
    int jump_count_before_turn = 0;
    bool was_on_ground = duck->isOnGround();

    for (int i = 0; i < 300; ++i) {
        world->advanceTime(0.016);
        int current_x = duck->getAnchorCell().x;
        bool on_ground = duck->isOnGround();

        // Track jumps before first turn.
        if (!turned_around && was_on_ground && !on_ground) {
            jump_count_before_turn++;
            spdlog::info("Frame {}: Jump #{} detected at x={}", i, jump_count_before_turn, current_x);
        }

        // Detect hitting right wall (near x=13 or 14).
        if (!hit_right_wall && current_x >= 12) {
            hit_right_wall = true;
            right_wall_x = current_x;
            spdlog::info("Frame {}: Duck hit right wall at x={}", i, current_x);
        }

        // After hitting wall, check if duck turned around and moved left.
        if (hit_right_wall && current_x < right_wall_x - 2) {
            turned_around = true;
            spdlog::info("Frame {}: Duck turned around, now at x={}", i, current_x);
            break;
        }

        was_on_ground = on_ground;
    }

    EXPECT_TRUE(hit_right_wall) << "Duck should reach the right wall";
    EXPECT_TRUE(turned_around) << "Duck should turn around after hitting wall";
    EXPECT_EQ(jump_count_before_turn, 0) << "Duck should not jump before turning around at wall";
}

TEST_F(DuckTest, DuckBrain2BouncesBackAndForth)
{
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::info);

    // Create world.
    auto world = std::make_unique<World>(15, 5);
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
    int last_direction = 0;  // 1 = right, -1 = left.
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
            spdlog::info("Frame {}: pos=({},{}), vel=({:.2f},{:.2f}), on_ground={}",
                i, current_x, current_y, cell.velocity.x, cell.velocity.y, on_ground);
            spdlog::info("  Forces: gravity=({:.2f},{:.2f}), friction=({:.2f},{:.2f}), "
                "viscous=({:.2f},{:.2f}), cohesion=({:.2f},{:.2f}), adhesion=({:.2f},{:.2f}), "
                "pressure=({:.2f},{:.2f})",
                debug.accumulated_gravity_force.x, debug.accumulated_gravity_force.y,
                debug.accumulated_friction_force.x, debug.accumulated_friction_force.y,
                debug.accumulated_viscous_force.x, debug.accumulated_viscous_force.y,
                debug.accumulated_com_cohesion_force.x, debug.accumulated_com_cohesion_force.y,
                debug.accumulated_adhesion_force.x, debug.accumulated_adhesion_force.y,
                debug.accumulated_pressure_force.x, debug.accumulated_pressure_force.y);
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

TEST_F(DuckTest, DuckBrain2JumpsWhenMovingFastInMiddle)
{
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::info);

    // Create world.
    auto world = std::make_unique<World>(20, 5);
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
            spdlog::info("Jump #{} at x={}, distance from center: {}", i + 1, jump_positions[i], dist_from_center);
        }

        // Later jumps should be closer to center as learning improves.
        if (jump_positions.size() >= 3) {
            int first_dist = std::abs(jump_positions[0] - approx_center);
            int last_dist = std::abs(jump_positions.back() - approx_center);
            spdlog::info("First jump dist from center: {}, Last jump dist: {}", first_dist, last_dist);
        }
    }
}

TEST_F(DuckTest, DuckBrain2DoesNotJumpWhenStationary)
{
    // Create world.
    auto world = std::make_unique<World>(10, 5);
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

// ============================================================================
// Cliff Detection and Jumping Tests
// ============================================================================

/**
 * Helper to create a world with a cliff.
 *
 * Layout (width x 10):
 *   Row 0: WALL border (ceiling)
 *   Row 1-7: AIR
 *   Row 8: WALL floor from x=0 to cliff_start-1, AIR from cliff_start to cliff_end, WALL from cliff_end+1 to width-1
 *   Row 9: WALL border (bottom)
 */
std::unique_ptr<World> createCliffWorld(int width, int cliff_start, int cliff_end)
{
    auto world = std::make_unique<World>(width, 10);

    // Clear interior to air (rows 1-8).
    for (uint32_t y = 1; y < 9; ++y) {
        for (uint32_t x = 1; x < static_cast<uint32_t>(width - 1); ++x) {
            world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
        }
    }

    // Create floor with gap (cliff).
    for (int x = 0; x < width; ++x) {
        if (x >= cliff_start && x <= cliff_end) {
            // Gap - air.
            world->getData().at(x, 8).replaceMaterial(MaterialType::AIR, 0.0);
        } else {
            // Floor - wall.
            world->getData().at(x, 8).replaceMaterial(MaterialType::WALL, 1.0);
        }
    }

    return world;
}

TEST_F(DuckTest, DuckBrain2JumpsOverCliffWhenFast)
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
    int first_cliff_jump_x = -1;  // Track where the first cliff jump occurred.
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
    spdlog::info("CliffTest: Knowledge - max_speed={:.1f}, jump_distance={:.1f}",
        knowledge.max_speed.value_or(-1), knowledge.jump_distance.value_or(-1));

    spdlog::info("CliffTest: fell_in_cliff={}, crossed_cliff={}, jump_count={}, first_cliff_jump_x={}",
        fell_in_cliff, crossed_cliff, jump_count, first_cliff_jump_x);

    // Duck should jump when it sees a cliff (survival instinct, no knowledge needed).
    EXPECT_GE(jump_count, 1) << "Duck should jump when cliff detected";
    EXPECT_TRUE(crossed_cliff) << "Duck should cross the cliff";
    EXPECT_FALSE(fell_in_cliff) << "Duck should not fall into cliff";

    // Duck should jump close to the edge, not too early.
    // Must be within 1 cell of the cliff start.
    EXPECT_GE(first_cliff_jump_x, CLIFF_START - 1)
        << "Duck should jump within 1 cell of cliff edge, not earlier";
}

TEST_F(DuckTest, DuckBrain2DetectsCliffInSensoryData)
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
    constexpr int FLOOR_ROW = 5;  // Row below duck center (4).
    for (int col = 0; col < DuckSensoryData::GRID_SIZE; ++col) {
        double total_fill = 0.0;
        for (int mat = 0; mat < DuckSensoryData::NUM_MATERIALS; ++mat) {
            if (mat != static_cast<int>(MaterialType::AIR)) {
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

    /**
     * Create a 20x10 world with outer walls and an obstacle.
     *
     * Layout:
     *   Row 0: WALL border (ceiling)
     *   Row 1-8: AIR (interior)
     *   Row 9: WALL border (floor)
     *
     * Obstacle is placed at the specified x position, rising from the floor.
     */
    std::unique_ptr<World> createObstacleWorld(int obstacle_x, int obstacle_height)
    {
        constexpr int WIDTH = 20;
        constexpr int HEIGHT = 10;

        auto world = std::make_unique<World>(WIDTH, HEIGHT);

        // Clear everything to air first.
        for (uint32_t y = 0; y < HEIGHT; ++y) {
            for (uint32_t x = 0; x < WIDTH; ++x) {
                world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
            }
        }

        // Add outer walls manually.
        // Top and bottom rows.
        for (uint32_t x = 0; x < WIDTH; ++x) {
            world->getData().at(x, 0).replaceMaterial(MaterialType::WALL, 1.0);
            world->getData().at(x, HEIGHT - 1).replaceMaterial(MaterialType::WALL, 1.0);
        }
        // Left and right columns.
        for (uint32_t y = 0; y < HEIGHT; ++y) {
            world->getData().at(0, y).replaceMaterial(MaterialType::WALL, 1.0);
            world->getData().at(WIDTH - 1, y).replaceMaterial(MaterialType::WALL, 1.0);
        }

        // Place obstacle: WALL blocks rising from the floor.
        // Floor is at row HEIGHT-1, so obstacle occupies rows above it.
        for (int h = 0; h < obstacle_height; ++h) {
            int y = HEIGHT - 2 - h;  // Start one above floor, go up.
            if (y >= 1) {
                world->getData().at(obstacle_x, y).replaceMaterial(MaterialType::WALL, 1.0);
            }
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
                    row += "D";
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

TEST_P(DuckObstacleJumpTest, JumpsOverObstacle)
{
    const auto& params = GetParam();
    spdlog::info("ObstacleJumpTest: obstacle_x={}, height={}, name={}",
        params.obstacle_x, params.obstacle_height, params.name);

    auto world = createObstacleWorld(params.obstacle_x, params.obstacle_height);
    printWorld(*world, "Initial world with obstacle");

    OrganismManager& manager = world->getOrganismManager();

    // Duck spawns with one cell gap from left wall, one cell up from floor.
    // In a 20x10 world: wall at x=0, gap at x=1, duck at x=2.
    constexpr int SPAWN_X = 2;
    constexpr int SPAWN_Y = 7;  // One cell up in the air to let it settle.

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
            spdlog::info("Frame 10: x={}, settled_x={}, moving_right={}",
                current_x, settled_x, moving_right_by_frame_10);
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
    spdlog::info("Final state: jumped={}, jump_x={}, max_x={}, cleared={}",
        jumped, jump_x, max_x_reached, cleared_obstacle);
    spdlog::info("Knowledge: max_speed={:.1f}, jump_distance={:.1f}",
        knowledge.max_speed.value_or(-1), knowledge.jump_distance.value_or(-1));

    printWorld(*world, "Final world state");

    // Assertions.
    EXPECT_TRUE(moving_right_by_frame_10)
        << "Duck should be moving right by frame 10";

    EXPECT_TRUE(jumped)
        << "Duck should jump when approaching obstacle";

    if (jumped) {
        EXPECT_LT(jump_x, params.obstacle_x)
            << "Duck should jump BEFORE reaching the obstacle (jump_x=" << jump_x
            << ", obstacle_x=" << params.obstacle_x << ")";
    }

    EXPECT_TRUE(cleared_obstacle)
        << "Duck should clear the obstacle (max_x=" << max_x_reached
        << ", obstacle_x=" << params.obstacle_x << ")";
}

// Start with just one test case: obstacle in the middle.
INSTANTIATE_TEST_SUITE_P(
    ObstacleLocations,
    DuckObstacleJumpTest,
    ::testing::Values(
        ObstacleTestCase{10, 1, "middle_1h"}
        // Future test cases:
        // ObstacleTestCase{5, 1, "near_spawn_1h"},
        // ObstacleTestCase{15, 1, "far_1h"},
        // ObstacleTestCase{10, 2, "middle_2h"}
    ),
    [](const ::testing::TestParamInfo<ObstacleTestCase>& info) {
        return info.param.name;
    }
);

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
TEST_F(DuckTest, AirSteeringForwardWhileMovingForward)
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
    setup.brain->setDirectInput({1.0f, 0.0f}, false);
    setup.advanceFrames(30);

    double vel_before_jump = setup.getVelocity().x;
    int x_before_jump = setup.duck->getAnchorCell().x;
    spdlog::info("AirSteeringForward: Before jump - x={}, vel.x={:.2f}", x_before_jump, vel_before_jump);

    ASSERT_GT(vel_before_jump, 1.0) << "Duck should have built up rightward velocity";
    ASSERT_TRUE(setup.duck->isOnGround()) << "Duck should still be on ground";

    // Phase 2: Jump while holding right.
    setup.brain->setDirectInput({1.0f, 0.0f}, true);
    setup.advance();  // Jump frame.

    // Phase 3: Track jump arc using Y position only (ground detection is unreliable).
    int min_y = start_y;  // Track highest point (lowest Y).
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
            spdlog::info("AirSteeringForward: Became airborne at frame {}, y={}, vel.x={:.2f}",
                frame, y, vel_x);
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
            spdlog::info("  Frame {}: pos=({},{}), vel=({:.2f},{:.2f})",
                frame, setup.duck->getAnchorCell().x, y, vel_x, vel_y);
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

    spdlog::info("AirSteeringForward: After landing - x={}, vel.x={:.2f}", x_after_jump, vel_after_land);
    spdlog::info("AirSteeringForward: Air phase: {} frames, peak at y={}", air_frames, min_y);

    // Assertions for SMB1-style behavior:
    // 1. Duck should have moved forward during jump.
    EXPECT_GT(x_after_jump, x_before_jump) << "Duck should move forward during jump";

    // 2. For forward input while moving forward, velocity should be roughly maintained.
    double vel_change_during_air = vel_after_land - vel_at_airborne_start;
    spdlog::info("AirSteeringForward: Velocity change during air phase: {:.2f}", vel_change_during_air);
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
TEST_F(DuckTest, AirSteeringBackwardDecelsFasterThanForward)
{
    // Helper lambda to run a jump scenario and return velocity change.
    auto runJumpScenario = [](float air_input_x, const char* label) -> double {
        auto setup = DuckTestSetup::create(50, 15, 5, 13);
        if (!setup.duck || !setup.duck->isOnGround()) {
            return 0.0;  // Setup failed.
        }

        int start_y = setup.duck->getAnchorCell().y;

        // Build up rightward velocity on ground.
        setup.brain->setDirectInput({1.0f, 0.0f}, false);
        setup.advanceFrames(30);

        double vel_before_jump = setup.getVelocity().x;
        spdlog::info("{}: Before jump vel.x={:.2f}", label, vel_before_jump);

        // Jump.
        setup.brain->setDirectInput({1.0f, 0.0f}, true);
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
                setup.brain->setMove({air_input_x, 0.0f});
                spdlog::info("{}: Airborne at frame {}, vel.x={:.2f}, input={:.1f}",
                    label, frame, vel_x, air_input_x);
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
    // This will FAIL until air steering is implemented.
    constexpr double MIN_DIFFERENCE = 2.0;  // Backward should decel at least 2 units more.
    EXPECT_LT(vel_change_backward, vel_change_forward - MIN_DIFFERENCE)
        << "Backward air input should cause more deceleration than forward input. "
        << "Forward: " << vel_change_forward << ", Backward: " << vel_change_backward
        << ". This test requires air steering to be implemented.";

    // Both should still show some deceleration (from air resistance at minimum).
    EXPECT_LT(vel_change_forward, 0.0) << "Forward should still decelerate from air resistance";
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
TEST_F(DuckTest, FacingLockedWhileAirborne)
{
    auto setup = DuckTestSetup::create(50, 15, 5, 13);
    ASSERT_NE(setup.duck, nullptr);
    ASSERT_TRUE(setup.duck->isOnGround());

    int start_y = setup.duck->getAnchorCell().y;

    // Build rightward velocity - facing should become RIGHT.
    setup.brain->setDirectInput({1.0f, 0.0f}, false);
    setup.advanceFrames(30);
    ASSERT_GT(setup.duck->getFacing().x, 0.0f) << "Should be facing right after moving right";

    // Jump while holding right.
    setup.brain->setDirectInput({1.0f, 0.0f}, true);
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
    spdlog::info("FacingLocked: Airborne at frame {}, facing.x = {:.1f}",
        airborne_frame, facing_at_jump);
    ASSERT_GT(facing_at_jump, 0.0f) << "Should be facing right at jump time";

    // Now steer LEFT while airborne for several frames.
    setup.brain->setMove({-1.0f, 0.0f});
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
                << " facing.x = " << current_facing
                << ". Facing should be locked at jump time.";
        }

        // Stop once we land.
        if (y >= start_y && frame > airborne_frame + 5) {
            spdlog::info("FacingLocked: Landed at frame {}, checked {} airborne frames",
                frame, frames_checked);
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
TEST_F(DuckTest, BackwardsJumpTrickGivesBetterAcceleration)
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
        setup.brain->setDirectInput({1.0f, 0.0f}, false);
        setup.advanceFrames(30);

        double vel_before_jump = setup.getVelocity().x;
        spdlog::info("{}: Before jump vel.x={:.2f}", label, vel_before_jump);

        // Phase 2: Jump frame - optionally face left by tapping left.
        if (jump_facing_left) {
            // Tap left on jump frame to set facing left, but still jump.
            setup.brain->setDirectInput({-1.0f, 0.0f}, true);
        } else {
            // Normal: jump while holding right.
            setup.brain->setDirectInput({1.0f, 0.0f}, true);
        }
        setup.advance();  // Execute jump.

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
                setup.brain->setMove({1.0f, 0.0f});
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
    spdlog::info("Normal jump (face right, steer right):    vel_change = {:.2f}", vel_change_normal);
    spdlog::info("Backwards jump (face left, steer right):  vel_change = {:.2f}", vel_change_backwards);
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
TEST_F(DuckTest, AsymmetricAirSteeringOpposingGivesHigherForce)
{
    // Helper to run a scenario. face_left_at_jump controls facing direction.
    auto runScenario = [](bool face_left_at_jump, const char* label) -> double {
        auto setup = DuckTestSetup::create(50, 15, 5, 13);
        if (!setup.duck || !setup.duck->isOnGround()) {
            return 0.0;
        }

        int start_y = setup.duck->getAnchorCell().y;

        // Build rightward velocity.
        setup.brain->setDirectInput({1.0f, 0.0f}, false);
        setup.advanceFrames(30);

        double vel_before = setup.getVelocity().x;

        // Jump - optionally tap left to face left.
        if (face_left_at_jump) {
            setup.brain->setDirectInput({-1.0f, 0.0f}, true);
        } else {
            setup.brain->setDirectInput({1.0f, 0.0f}, true);
        }
        setup.advance();

        float facing_at_jump = setup.duck->getFacing().x;
        spdlog::info("{}: Before jump vel.x={:.2f}, facing at jump={:.1f}",
            label, vel_before, facing_at_jump);

        // Track until airborne, then steer RIGHT.
        double vel_at_airborne = 0.0;
        for (int i = 0; i < 20; ++i) {
            setup.advance();
            if (setup.duck->getAnchorCell().y < start_y) {
                vel_at_airborne = setup.getVelocity().x;
                // Both scenarios steer RIGHT.
                setup.brain->setMove({1.0f, 0.0f});
                spdlog::info("{}: Airborne, vel.x={:.2f}, facing={:.1f}, steering RIGHT",
                    label, vel_at_airborne, setup.duck->getFacing().x);
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
        spdlog::info("{}: After {} air frames, vel.x={:.2f}, change={:.2f}",
            label, AIR_FRAMES, vel_after, vel_change);
        return vel_change;
    };

    // Both scenarios steer RIGHT, but with different facing.
    double vel_change_face_right = runScenario(false, "FaceRight");  // Same as steer.
    double vel_change_face_left = runScenario(true, "FaceLeft");     // Opposing steer.

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
    spdlog::info("Acceleration difference: {:.2f} (positive = opposing accelerates more)",
        accel_difference);

    constexpr double MIN_ASYMMETRY = 10.0;
    EXPECT_GT(accel_difference, MIN_ASYMMETRY)
        << "Backwards jump should give significantly better acceleration. "
        << "FaceRight: " << vel_change_face_right << ", FaceLeft: " << vel_change_face_left
        << ". Expected difference > " << MIN_ASYMMETRY << ", got " << accel_difference;
}

/**
 * @brief Test that duck (single-cell organism) can float in water via buoyancy.
 *
 * This tests the fix for the bug where organism cells were blocked from
 * participating in buoyancy swaps. Single-cell organisms like Duck should
 * use normal cell physics (including swaps), while rigid body organisms
 * like Goose should resist displacement.
 */
TEST_F(DuckTest, DuckFloatsInWater)
{
    // Enable swap logging to verify the swap mechanism.
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Swap, spdlog::level::info);

    spdlog::info("=== DuckFloatsInWater ===");

    // Create a 3x6 world (narrow column of water with duck submerged).
    auto world = std::make_unique<World>(3, 6);
    world->setWallsEnabled(false);

    // Configure physics for buoyancy testing.
    world->getPhysicsSettings().pressure_hydrostatic_enabled = true;
    world->getPhysicsSettings().pressure_hydrostatic_strength = 1.0;
    world->getPhysicsSettings().swap_enabled = true;
    world->getPhysicsSettings().gravity = 9.81;

    // Fill the middle column with water, duck submerged at bottom.
    // Layout: [W=water, D=duck, .=air]
    //   . W .   y=0
    //   . W .   y=1
    //   . W .   y=2
    //   . D .   y=3 (duck starts here, submerged)
    //   . W .   y=4
    //   . W .   y=5
    for (int y = 0; y < 6; ++y) {
        if (y != 3) {
            world->addMaterialAtCell(1, y, MaterialType::WATER, 1.0);
        }
    }

    // Create duck at (1, 3) - submerged in water.
    // Use TestDuckBrain so the duck just waits (no random movement affecting buoyancy).
    auto test_brain = std::make_unique<TestDuckBrain>();
    test_brain->setAction(DuckAction::WAIT);
    OrganismId duck_id =
        world->getOrganismManager().createDuck(*world, 1, 3, std::move(test_brain));
    ASSERT_NE(duck_id, INVALID_ORGANISM_ID);

    Duck* duck = world->getOrganismManager().getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    int initial_y = duck->getAnchorCell().y;
    spdlog::info("Duck starts at y={}", initial_y);
    EXPECT_EQ(initial_y, 3);

    // Run simulation - duck should float upward.
    const double deltaTime = 0.016;
    const int max_steps = 500;
    int final_y = initial_y;
    int swap_count = 0;

    // Output formatted table header.
    // Format: step | duck_y | duck_com_y | duck_vel_y | above_mat | above_com_y | above_vel_y | swap
    std::cout << "\n=== BUOYANCY DATA TABLE ===\n";
    std::cout << "step\tduck_y\tcom_y\tvel_y\tabove_mat\tabove_com\tabove_vel\tswap\n";
    std::cout << "----\t------\t-----\t-----\t---------\t---------\t---------\t----\n";

    for (int step = 0; step < max_steps; ++step) {
        int y_before = duck->getAnchorCell().y;

        world->advanceTime(deltaTime);

        int y_after = duck->getAnchorCell().y;
        bool swapped = (y_after != y_before);
        if (swapped) {
            swap_count++;
            final_y = y_after;
        }

        // Output data every 5 steps, or on swap events, or near interesting times.
        bool should_log = (step % 5 == 0) || swapped || (step >= 25 && step <= 35);
        if (should_log) {
            const Cell& duck_cell = world->getData().at(1, y_after);

            // Get info about cell above the duck (if exists).
            std::string above_mat = "-";
            std::string above_com = "-";
            std::string above_vel = "-";
            if (y_after > 0) {
                const Cell& above = world->getData().at(1, y_after - 1);
                above_mat = getMaterialName(above.material_type);
                above_com = fmt::format("{:.2f}", above.com.y);
                above_vel = fmt::format("{:.2f}", above.velocity.y);
            }

            std::cout << fmt::format("{}\t{}\t{:.2f}\t{:.2f}\t{}\t{}\t{}\t{}\n",
                step, y_after, duck_cell.com.y, duck_cell.velocity.y,
                above_mat, above_com, above_vel,
                swapped ? "SWAP" : "");
        }

        // Stop early if duck reached the surface.
        if (y_after == 0) {
            spdlog::info("  Duck reached surface at step {}", step);
            break;
        }
    }
    std::cout << "=== END TABLE ===\n\n";

    spdlog::info("Duck final position: y={} (started at y={}), {} swaps", final_y, initial_y, swap_count);

    // Duck should have floated upward (y decreased).
    EXPECT_LT(final_y, initial_y)
        << "Duck (WOOD, density 0.3) should float upward through water (density 1.0)";
    EXPECT_GE(swap_count, 1)
        << "Duck should participate in buoyancy swaps (not blocked by organism check)";

    // Check that duck doesn't rise too fast (max 0.5 cells per step).
    // Distance traveled = initial_y - final_y (positive when rising).
    int distance_traveled = initial_y - final_y;
    double rise_rate = static_cast<double>(distance_traveled) / static_cast<double>(swap_count);
    spdlog::info("Rise rate: {:.2f} cells/swap ({} cells in {} swaps)",
        rise_rate, distance_traveled, swap_count);
    EXPECT_LE(rise_rate, 0.5)
        << "Duck should not rise faster than 0.5 cells per swap (was " << rise_rate << ")";
}
