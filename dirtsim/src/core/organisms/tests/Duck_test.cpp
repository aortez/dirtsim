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
 */
class TestDuckBrain : public DuckBrain {
public:
    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override
    {
        (void)sensory;
        (void)deltaTime;

        switch (current_action_) {
            case DuckAction::RUN_LEFT:
                duck.setWalkDirection(-1.0f);
                break;
            case DuckAction::RUN_RIGHT:
                duck.setWalkDirection(1.0f);
                break;
            case DuckAction::JUMP:
                duck.jump();
                break;
            case DuckAction::WAIT:
            default:
                duck.setWalkDirection(0.0f);
                break;
        }
    }

    void setAction(DuckAction action) { current_action_ = action; }
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
    EXPECT_EQ(cell.organism_id, duck_id);

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
    // Layout (20x5):
    //   Row 0: WALL border
    //   Row 1-3: AIR
    //   Row 4: WALL floor
    auto world = std::make_unique<World>(20, 5);

    // Clear interior to air.
    for (uint32_t y = 1; y < 4; ++y) {
        for (uint32_t x = 1; x < 19; ++x) {
            world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
        }
    }

    // Ensure floor.
    for (uint32_t x = 0; x < 20; ++x) {
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
    spdlog::info("Duck settled at y={}", settled_y);

    // Trigger jump.
    brain_ptr->setAction(DuckAction::JUMP);
    world->advanceTime(0.016);  // One frame to initiate jump.

    // Switch to wait so we don't keep trying to jump.
    brain_ptr->setAction(DuckAction::WAIT);

    // Track the highest point (minimum Y since Y increases downward).
    int min_y = settled_y;

    // Run physics for enough frames to complete the jump arc.
    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);

        int current_y = duck->getAnchorCell().y;
        if (current_y < min_y) {
            min_y = current_y;
        }
    }

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
