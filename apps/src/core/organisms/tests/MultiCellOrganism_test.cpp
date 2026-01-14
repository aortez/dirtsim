/**
 * Multi-cell organism validation tests.
 *
 * Validates rigid body components work correctly for multi-cell organisms before
 * migrating Tree. Uses simple test shapes (Stick, LShape, Column) with no growth
 * or brain logic - just pure physics.
 *
 * Behaviors verified:
 * 1. Cells stay together when falling/moving (unified velocity)
 * 2. Ground support with multiple contact points
 * 3. Friction from multiple ground contacts
 * 4. Collision detection for multi-cell shapes
 * 5. Center of mass computed correctly
 * 6. No tearing during horizontal motion
 */

#include "CellTrackerUtil.h"
#include "MultiCellTestOrganism.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>
#include <iomanip>
#include <spdlog/spdlog.h>

using namespace DirtSim;

class MultiCellOrganismTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::info); }

    std::unique_ptr<World> createTestWorld(int width = 20, int height = 15)
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

    void printOrganismState(int frame, const MultiCellTestOrganism* org)
    {
        Vector2i anchor = org->getAnchorCell();
        auto cells = org->getGridPositions();

        std::cout << std::setw(3) << frame << " | " << "pos=(" << std::fixed << std::setprecision(2)
                  << std::setw(6) << org->position.x << "," << std::setw(5) << org->position.y
                  << ") | " << "anchor=(" << std::setw(2) << anchor.x << "," << anchor.y << ") | "
                  << "vel=(" << std::setw(6) << org->velocity.x << "," << std::setw(6)
                  << org->velocity.y << ") | " << "ground=" << (org->isOnGround() ? "Y" : "N")
                  << " | cells=" << cells.size() << "\n";
    }
};

// =============================================================================
// Stick Tests (2 horizontal cells)
// =============================================================================

TEST_F(MultiCellOrganismTest, StickFallsAsUnit)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    OrganismId id = manager.createMultiCellTestOrganism(*world, 10, 3, MultiCellShape::STICK);
    auto* stick = manager.getMultiCellTestOrganism(id);
    ASSERT_NE(stick, nullptr);

    std::cout << "Initial state:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    CellTracker tracker(*world, id, 20);
    for (const auto& pos : stick->getGridPositions()) {
        tracker.trackCell(pos, Material::EnumType::Wood, 0);
    }

    // Run physics - stick should fall.
    for (int frame = 0; frame < 200; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(frame);

        // Track current cells.
        for (const auto& pos : stick->getGridPositions()) {
            tracker.trackCell(pos, Material::EnumType::Wood, frame);
        }

        // Check displacement every 20 frames.
        if (frame % 20 == 0) {
            tracker.checkForDisplacements(frame);
        }
    }

    std::cout << "After 200 frames:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Stick should be at floor level.
    int expected_y = world->getData().height - 2;
    EXPECT_EQ(stick->getAnchorCell().y, expected_y)
        << "Stick should have fallen to rest at y=" << expected_y;

    // Both cells should be at the same y position (no tearing).
    auto cells = stick->getGridPositions();
    ASSERT_EQ(cells.size(), 2u);
    EXPECT_EQ(cells[0].y, cells[1].y) << "Both cells should be at same Y (no tearing)";

    // Cells should be adjacent horizontally.
    EXPECT_EQ(std::abs(cells[0].x - cells[1].x), 1) << "Cells should be horizontally adjacent";

    EXPECT_TRUE(stick->isOnGround()) << "Stick should be on ground";

    if (HasFailure()) {
        std::cout << "\n=== STICK FALLING DEBUG ===\n";
        tracker.printTableHeader();
        for (int f = 0; f < 200; f += 20) {
            tracker.printTableRow(f, true);
        }
    }
}

TEST_F(MultiCellOrganismTest, StickMovesHorizontallyWithoutTearing)
{
    auto world = createTestWorld(40, 15);
    OrganismManager& manager = world->getOrganismManager();

    int floor_y = world->getData().height - 2;
    OrganismId id = manager.createMultiCellTestOrganism(*world, 5, floor_y, MultiCellShape::STICK);
    auto* stick = manager.getMultiCellTestOrganism(id);
    ASSERT_NE(stick, nullptr);

    // Let it settle.
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    std::cout << "After settling:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    ASSERT_TRUE(stick->isOnGround()) << "Stick should be on ground before horizontal test";

    CellTracker tracker(*world, id, 50);
    for (const auto& pos : stick->getGridPositions()) {
        tracker.trackCell(pos, Material::EnumType::Wood, 0);
    }

    // Apply horizontal force.
    stick->setExternalForce(Vector2d{ 50.0, 0.0 });
    int start_x = stick->getAnchorCell().x;

    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(frame);

        // Check structural integrity.
        auto cells = stick->getGridPositions();
        ASSERT_EQ(cells.size(), 2u) << "Stick should always have 2 cells";
        EXPECT_EQ(cells[0].y, cells[1].y)
            << "Cells should stay at same Y during horizontal motion at frame " << frame;
        EXPECT_EQ(std::abs(cells[0].x - cells[1].x), 1)
            << "Cells should remain horizontally adjacent at frame " << frame;

        for (const auto& pos : cells) {
            tracker.trackCell(pos, Material::EnumType::Wood, frame);
        }
    }

    std::cout << "After 100 frames with horizontal force:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    int end_x = stick->getAnchorCell().x;
    int distance = end_x - start_x;

    EXPECT_GT(distance, 5) << "Stick should have moved at least 5 cells horizontally";
    EXPECT_EQ(stick->getAnchorCell().y, floor_y) << "Stick should stay on floor";

    if (HasFailure()) {
        std::cout << "\n=== STICK HORIZONTAL MOTION DEBUG ===\n";
        std::cout << "Moved from x=" << start_x << " to x=" << end_x << "\n";
        printOrganismState(100, stick);
        tracker.printTableHeader();
        for (int f = 0; f < 100; f += 10) {
            tracker.printTableRow(f, true);
        }
    }
}

// =============================================================================
// LShape Tests (3 cells in L configuration)
// =============================================================================

TEST_F(MultiCellOrganismTest, LShapeFallsAsUnit)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    OrganismId id = manager.createMultiCellTestOrganism(*world, 10, 5, MultiCellShape::LSHAPE);
    auto* lshape = manager.getMultiCellTestOrganism(id);
    ASSERT_NE(lshape, nullptr);

    std::cout << "Initial state:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    CellTracker tracker(*world, id, 20);
    for (const auto& pos : lshape->getGridPositions()) {
        tracker.trackCell(pos, Material::EnumType::Wood, 0);
    }

    for (int frame = 0; frame < 200; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(frame);

        for (const auto& pos : lshape->getGridPositions()) {
            tracker.trackCell(pos, Material::EnumType::Wood, frame);
        }
    }

    std::cout << "After 200 frames:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // LShape should be at floor.
    int expected_y = world->getData().height - 2;
    EXPECT_EQ(lshape->getAnchorCell().y, expected_y)
        << "LShape anchor should be at floor level y=" << expected_y;

    // All 3 cells should maintain L shape.
    auto cells = lshape->getGridPositions();
    ASSERT_EQ(cells.size(), 3u) << "LShape should have 3 cells";

    // Verify L shape structure.
    int bottom_count = 0;
    int top_count = 0;
    int min_y = cells[0].y;
    for (const auto& c : cells) {
        if (c.y < min_y) min_y = c.y;
    }
    for (const auto& c : cells) {
        if (c.y == min_y)
            top_count++;
        else
            bottom_count++;
    }

    EXPECT_EQ(top_count, 1) << "L shape should have 1 cell in top row";
    EXPECT_EQ(bottom_count, 2) << "L shape should have 2 cells in bottom row";

    EXPECT_TRUE(lshape->isOnGround()) << "LShape should be on ground";

    if (HasFailure()) {
        std::cout << "\n=== LSHAPE FALLING DEBUG ===\n";
        std::cout << "Cells: ";
        for (const auto& c : cells) {
            std::cout << "(" << c.x << "," << c.y << ") ";
        }
        std::cout << "\n";
        tracker.printTableHeader();
        for (int f = 0; f < 200; f += 20) {
            tracker.printTableRow(f, true);
        }
    }
}

TEST_F(MultiCellOrganismTest, LShapeCollidesWithWall)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    // Add a vertical wall.
    int wall_x = 15;
    for (int y = 1; y < world->getData().height - 1; ++y) {
        world->getData().at(wall_x, y).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    int floor_y = world->getData().height - 2;
    OrganismId id = manager.createMultiCellTestOrganism(*world, 8, floor_y, MultiCellShape::LSHAPE);
    auto* lshape = manager.getMultiCellTestOrganism(id);
    ASSERT_NE(lshape, nullptr);

    // Let it settle.
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    std::cout << "Before moving toward wall:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    CellTracker tracker(*world, id, 50);

    // Push toward wall.
    lshape->setExternalForce(Vector2d{ 100.0, 0.0 });

    for (int frame = 0; frame < 200; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(frame);
    }

    std::cout << "After 200 frames:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // LShape should have stopped before the wall.
    auto cells = lshape->getGridPositions();
    for (const auto& cell : cells) {
        EXPECT_LT(cell.x, wall_x) << "LShape cell at (" << cell.x << "," << cell.y
                                  << ") should not overlap wall at x=" << wall_x;
    }

    // Should be close to wall.
    int max_x = 0;
    for (const auto& c : cells) {
        if (c.x > max_x) max_x = c.x;
    }
    EXPECT_GE(max_x, wall_x - 3) << "LShape should have approached the wall";
}

// =============================================================================
// Column Tests (3 vertical cells)
// =============================================================================

TEST_F(MultiCellOrganismTest, ColumnFallsAsUnit)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    OrganismId id = manager.createMultiCellTestOrganism(*world, 10, 5, MultiCellShape::COLUMN);
    auto* column = manager.getMultiCellTestOrganism(id);
    ASSERT_NE(column, nullptr);

    std::cout << "Initial state:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    CellTracker tracker(*world, id, 20);
    for (const auto& pos : column->getGridPositions()) {
        tracker.trackCell(pos, Material::EnumType::Wood, 0);
    }

    for (int frame = 0; frame < 200; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(frame);

        // Check all cells maintain vertical alignment.
        auto cells = column->getGridPositions();
        if (cells.size() == 3) {
            EXPECT_EQ(cells[0].x, cells[1].x)
                << "Column cells should have same X at frame " << frame;
            EXPECT_EQ(cells[1].x, cells[2].x)
                << "Column cells should have same X at frame " << frame;
        }

        for (const auto& pos : cells) {
            tracker.trackCell(pos, Material::EnumType::Wood, frame);
        }
    }

    std::cout << "After 200 frames:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Bottom of column should be at floor.
    int expected_y = world->getData().height - 2;
    EXPECT_EQ(column->getAnchorCell().y, expected_y)
        << "Column anchor (bottom) should be at floor level";

    // All cells should be vertically aligned.
    auto cells = column->getGridPositions();
    ASSERT_EQ(cells.size(), 3u) << "Column should have 3 cells";

    int x = cells[0].x;
    for (const auto& c : cells) {
        EXPECT_EQ(c.x, x) << "All column cells should have same X coordinate";
    }

    EXPECT_TRUE(column->isOnGround()) << "Column should be on ground";

    if (HasFailure()) {
        std::cout << "\n=== COLUMN FALLING DEBUG ===\n";
        std::cout << "Cells: ";
        for (const auto& c : cells) {
            std::cout << "(" << c.x << "," << c.y << ") ";
        }
        std::cout << "\n";
        tracker.printTableHeader();
        for (int f = 0; f < 200; f += 20) {
            tracker.printTableRow(f, true);
        }
    }
}

// =============================================================================
// Center of Mass Tests
// =============================================================================

TEST_F(MultiCellOrganismTest, StickCenterOfMassIsCentered)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    OrganismId id = manager.createMultiCellTestOrganism(*world, 10, 8, MultiCellShape::STICK);
    auto* stick = manager.getMultiCellTestOrganism(id);
    ASSERT_NE(stick, nullptr);

    stick->recomputeCenterOfMass();

    // Stick is 2 cells: (0,0) and (1,0). COM should be at (0.5, 0).
    EXPECT_NEAR(stick->center_of_mass.x, 0.5, 0.01) << "Stick COM.x should be 0.5 (centered)";
    EXPECT_NEAR(stick->center_of_mass.y, 0.0, 0.01) << "Stick COM.y should be 0";
}

TEST_F(MultiCellOrganismTest, LShapeCenterOfMassIsCorrect)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    OrganismId id = manager.createMultiCellTestOrganism(*world, 10, 8, MultiCellShape::LSHAPE);
    auto* lshape = manager.getMultiCellTestOrganism(id);
    ASSERT_NE(lshape, nullptr);

    lshape->recomputeCenterOfMass();

    // LShape cells: (0,-1), (0,0), (1,0). COM = ((0+0+1)/3, (-1+0+0)/3) = (0.333, -0.333).
    EXPECT_NEAR(lshape->center_of_mass.x, 0.333, 0.05) << "LShape COM.x should be ~0.333";
    EXPECT_NEAR(lshape->center_of_mass.y, -0.333, 0.05) << "LShape COM.y should be ~-0.333";
}

TEST_F(MultiCellOrganismTest, ColumnCenterOfMassIsCorrect)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    OrganismId id = manager.createMultiCellTestOrganism(*world, 10, 8, MultiCellShape::COLUMN);
    auto* column = manager.getMultiCellTestOrganism(id);
    ASSERT_NE(column, nullptr);

    column->recomputeCenterOfMass();

    // Column cells: (0,-2), (0,-1), (0,0). COM = (0, -1).
    EXPECT_NEAR(column->center_of_mass.x, 0.0, 0.01) << "Column COM.x should be 0";
    EXPECT_NEAR(column->center_of_mass.y, -1.0, 0.01) << "Column COM.y should be -1";
}

// =============================================================================
// Mass Computation Tests
// =============================================================================

TEST_F(MultiCellOrganismTest, MassScalesWithCellCount)
{
    auto world = createTestWorld();
    OrganismManager& manager = world->getOrganismManager();

    OrganismId stick_id = manager.createMultiCellTestOrganism(*world, 5, 8, MultiCellShape::STICK);
    OrganismId lshape_id =
        manager.createMultiCellTestOrganism(*world, 10, 8, MultiCellShape::LSHAPE);
    OrganismId column_id =
        manager.createMultiCellTestOrganism(*world, 15, 8, MultiCellShape::COLUMN);

    auto* stick = manager.getMultiCellTestOrganism(stick_id);
    auto* lshape = manager.getMultiCellTestOrganism(lshape_id);
    auto* column = manager.getMultiCellTestOrganism(column_id);

    ASSERT_NE(stick, nullptr);
    ASSERT_NE(lshape, nullptr);
    ASSERT_NE(column, nullptr);

    double wood_density = Material::getDensity(Material::EnumType::Wood);

    EXPECT_NEAR(stick->mass, 2 * wood_density, 0.01) << "Stick mass should be 2 * wood_density";
    EXPECT_NEAR(lshape->mass, 3 * wood_density, 0.01) << "LShape mass should be 3 * wood_density";
    EXPECT_NEAR(column->mass, 3 * wood_density, 0.01) << "Column mass should be 3 * wood_density";

    // LShape and Column should have same mass (both 3 cells).
    EXPECT_NEAR(lshape->mass, column->mass, 0.01) << "LShape and Column should have equal mass";
}

// =============================================================================
// Ground Friction Tests
// =============================================================================

TEST_F(MultiCellOrganismTest, StickDeceleratesWithFriction)
{
    auto world = createTestWorld(40, 15);
    OrganismManager& manager = world->getOrganismManager();

    int floor_y = world->getData().height - 2;
    OrganismId id = manager.createMultiCellTestOrganism(*world, 10, floor_y, MultiCellShape::STICK);
    auto* stick = manager.getMultiCellTestOrganism(id);
    ASSERT_NE(stick, nullptr);

    // Let it settle.
    for (int i = 0; i < 20; ++i) {
        world->advanceTime(0.016);
    }

    ASSERT_TRUE(stick->isOnGround());

    CellTracker tracker(*world, id, 50);

    // Apply horizontal force to build velocity.
    stick->setExternalForce(Vector2d{ 20.0, 0.0 });

    for (int frame = 0; frame < 100; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(frame);
    }

    // Stop force and measure deceleration.
    stick->setExternalForce(Vector2d{ 0.0, 0.0 });
    double velocity_at_stop = stick->velocity.x;

    for (int frame = 0; frame < 50; ++frame) {
        world->advanceTime(0.016);
        tracker.recordFrame(100 + frame);
    }

    double final_velocity = stick->velocity.x;

    std::cout << "Stick friction test:\n";
    std::cout << "  Velocity when force stopped: " << velocity_at_stop << "\n";
    std::cout << "  Final velocity: " << final_velocity << "\n";

    // Should have decelerated significantly due to friction.
    EXPECT_LT(final_velocity, velocity_at_stop * 0.5)
        << "Friction should reduce velocity by at least 50%";

    if (HasFailure()) {
        tracker.printTableHeader();
        for (int f = 90; f < 150; f += 5) {
            tracker.printTableRow(f, true);
        }
    }
}
