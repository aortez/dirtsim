/**
 * @file Duck_test.cpp
 * @brief Basic duck physics tests: creation, falling, tracking, walking, ground detection.
 *
 * For brain behavior tests, see DuckBrain_test.cpp.
 * For jumping/air steering tests, see DuckJump_test.cpp.
 * For buoyancy tests, see DuckBuoyancy_test.cpp.
 */

#include "DuckTestUtils.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/Duck.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace DirtSim;
using namespace DirtSim::Test;

class DuckTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::debug); }

    // Small 5x5 test world for basic tests.
    std::unique_ptr<World> createTestWorld() { return createFlatWorld(5, 5); }
};

// ============================================================================
// Basic Duck Creation and Physics Tests
// ============================================================================

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
    EXPECT_EQ(cell.material_type, Material::EnumType::Wood);
    EXPECT_EQ(manager.at(Vector2i{ 2, 2 }), duck_id);

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

        spdlog::info(
            "Frame {}: anchor_cell=({},{})",
            frame,
            duck->getAnchorCell().x,
            duck->getAnchorCell().y);
    }

    printWorld(*world, "After 40 frames");

    // The duck cell should have gained downward velocity or moved.
    // Check if the cell at (2,1) still has WOOD or if it transferred.
    const Cell& cell_at_start = world->getData().at(2, 1);
    const Cell& cell_below = world->getData().at(2, 2);
    const Cell& cell_at_floor = world->getData().at(2, 3);

    spdlog::info(
        "Cell (2,1): type={}, fill={}",
        static_cast<int>(cell_at_start.material_type),
        cell_at_start.fill_ratio);
    spdlog::info(
        "Cell (2,2): type={}, fill={}",
        static_cast<int>(cell_below.material_type),
        cell_below.fill_ratio);
    spdlog::info(
        "Cell (2,3): type={}, fill={}",
        static_cast<int>(cell_at_floor.material_type),
        cell_at_floor.fill_ratio);

    // Duck should have fallen - WOOD should be at a lower position.
    bool wood_moved_down =
        (cell_below.material_type == Material::EnumType::Wood
         || cell_at_floor.material_type == Material::EnumType::Wood);

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
    for (int y = 0; y < world->getData().height; ++y) {
        for (int x = 0; x < world->getData().width; ++x) {
            if (world->getData().at(x, y).material_type == Material::EnumType::Wood) {
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

    EXPECT_FALSE(duck->isOnGround()) << "Duck not on ground at start";

    // Run several frames - duck will fall and hit ground.
    for (int i = 0; i < 50; ++i) {
        world->advanceTime(0.016);
    }

    printWorld(*world, "After 50 frames - duck should be on ground");

    // By now the duck should have fallen and be resting on the wall.
    const Cell& cell = world->getData().at(duck->getAnchorCell().x, duck->getAnchorCell().y);
    spdlog::info(
        "Duck at ({},{}), velocity=({},{}), on_ground={}",
        duck->getAnchorCell().x,
        duck->getAnchorCell().y,
        cell.velocity.x,
        cell.velocity.y,
        duck->isOnGround());

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
    EXPECT_EQ(world->getData().at(2, 2).material_type, Material::EnumType::Wood);

    // Remove organism and its cells from the world.
    manager.removeOrganismFromWorld(*world, duck_id);

    printWorld(*world, "After duck removal");

    // Verify cell is now empty.
    const Cell& cell = world->getData().at(2, 2);
    EXPECT_EQ(cell.material_type, Material::EnumType::Air);
    EXPECT_LT(cell.fill_ratio, 0.01) << "Cell should be empty after duck removal";
}

// ============================================================================
// Walking Tests
// ============================================================================

TEST_F(DuckTest, DuckWalksWhenOnGround)
{
    auto world = createFlatWorld(100, 5);
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

    spdlog::info(
        "Duck walked from x={} to x={}, distance={} cells", start_x, final_x, distance_moved);

    // Should have moved at least 1-2 cells after 100 frames (~1.6 seconds).
    EXPECT_GE(distance_moved, 1) << "Duck should move at least 1 cell when walking for 100 frames";
}

TEST_F(DuckTest, DuckWalkingSpeedOnDifferentSurfaces)
{
    // Compare walking speed on different surfaces.
    // Track both distance and velocity to understand friction and air resistance effects.
    // Also test with DuckBrain2 to see what max speed it learns.

    struct SurfaceTestCase {
        Material::EnumType material;
        const char* name;
    };

    std::vector<SurfaceTestCase> test_cases = {
        { Material::EnumType::Wall, "WALL" },
        { Material::EnumType::Dirt, "DIRT" },
        { Material::EnumType::Sand, "SAND" },
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
        auto world = createFlatWorld(100, 10);

        // Override floor with test material.
        for (int x = 0; x < 100; ++x) {
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
    spdlog::info(
        "{:8} {:>10} {:>12} {:>12} {:>12}", "Surface", "Distance", "Vel@20", "Vel@80", "MaxVel");
    for (const auto& r : results) {
        spdlog::info(
            "{:8} {:>10} {:>12.1f} {:>12.1f} {:>12.1f}",
            r.name,
            r.distance,
            r.velocity_at_frame_20,
            r.velocity_at_frame_80,
            r.max_velocity);
    }

    // Check if velocity plateaued (air resistance) or kept growing.
    for (const auto& r : results) {
        if (r.velocity_at_frame_20 > 0.1) {
            double ratio = r.velocity_at_frame_80 / r.velocity_at_frame_20;
            if (ratio > 2.0) {
                spdlog::warn("{}: Velocity grew {}x - no terminal velocity", r.name, ratio);
            }
            else {
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
        auto world = createFlatWorld(100, 10);
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
                    spdlog::info(
                        "DuckBrain2 frame {}: pos={}, velocity.x={:.1f}",
                        frame,
                        pos.x,
                        cell.velocity.x);
                }
            }
        }

        // The "Learned max speed" log message from DuckBrain2 will appear in output.
        // We expect it to be around 50 cells/sec based on our earlier findings.
        spdlog::info("DuckBrain2 test complete. Check logs for 'Learned max speed' message.");
    }
}
