#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/Duck.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/DuckSensoryData.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/OrganismSensoryData.h"
#include <gtest/gtest.h>

using namespace DirtSim;

// =============================================================================
// SensoryUtils::gatherMaterialHistograms tests
// =============================================================================

/**
 * Test that gatherMaterialHistograms correctly samples materials at known positions.
 */
TEST(SensoryUtilsTest, GatherHistogramsCorrectlySamplesMaterials)
{
    // Create 15x15 world.
    auto world = std::make_unique<World>(15, 15);

    // Clear to air.
    for (uint32_t y = 0; y < 15; y++) {
        for (uint32_t x = 0; x < 15; x++) {
            world->getData().at(x, y) = Cell();
        }
    }

    // Place known materials at specific positions.
    world->addMaterialAtCell(7, 7, MaterialType::DIRT, 1.0);  // Center.
    world->addMaterialAtCell(5, 7, MaterialType::WATER, 0.8); // Left of center.
    world->addMaterialAtCell(9, 7, MaterialType::SAND, 0.6);  // Right of center.
    world->addMaterialAtCell(7, 5, MaterialType::WOOD, 1.0);  // Above center.
    world->addMaterialAtCell(7, 9, MaterialType::METAL, 1.0); // Below center.

    // Gather histograms centered at (7,7) with 9x9 grid.
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};
    Vector2i world_offset;
    SensoryUtils::gatherMaterialHistograms<9, 10>(*world, Vector2i{ 7, 7 }, histograms, world_offset);

    // Grid center is at (4,4) in neural coords.
    // World offset should be (7-4, 7-4) = (3, 3).
    EXPECT_EQ(world_offset.x, 3);
    EXPECT_EQ(world_offset.y, 3);

    // Check center (neural 4,4 -> world 7,7) has DIRT.
    EXPECT_GT(histograms[4][4][static_cast<int>(MaterialType::DIRT)], 0.9);

    // Check left (neural 2,4 -> world 5,7) has WATER.
    EXPECT_GT(histograms[4][2][static_cast<int>(MaterialType::WATER)], 0.7);

    // Check right (neural 6,4 -> world 9,7) has SAND.
    EXPECT_GT(histograms[4][6][static_cast<int>(MaterialType::SAND)], 0.5);

    // Check above (neural 4,2 -> world 7,5) has WOOD.
    EXPECT_GT(histograms[2][4][static_cast<int>(MaterialType::WOOD)], 0.9);

    // Check below (neural 4,6 -> world 7,9) has METAL.
    EXPECT_GT(histograms[6][4][static_cast<int>(MaterialType::METAL)], 0.9);

    // Check an empty cell (neural 0,0 -> world 3,3) is AIR/empty.
    double total_fill = 0.0;
    for (int m = 0; m < 10; m++) {
        total_fill += histograms[0][0][m];
    }
    EXPECT_LT(total_fill, 0.1);
}

/**
 * Test that gatherMaterialHistograms marks out-of-bounds as WALL.
 */
TEST(SensoryUtilsTest, GatherHistogramsMarksBoundariesAsWall)
{
    // Create small 10x10 world.
    auto world = std::make_unique<World>(10, 10);

    // Clear to air.
    for (uint32_t y = 0; y < 10; y++) {
        for (uint32_t x = 0; x < 10; x++) {
            world->getData().at(x, y) = Cell();
        }
    }

    // Request 9x9 grid centered at (1,1) - extends to (-3,-3) which is OOB.
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};
    Vector2i world_offset;
    SensoryUtils::gatherMaterialHistograms<9, 10>(*world, Vector2i{ 1, 1 }, histograms, world_offset);

    // Offset is centered on organism: (1-4, 1-4) = (-3, -3).
    EXPECT_EQ(world_offset.x, -3);
    EXPECT_EQ(world_offset.y, -3);

    // Top-left corner (neural [0][0]) maps to world (-3,-3) which is OOB, should be WALL.
    EXPECT_GT(histograms[0][0][static_cast<int>(MaterialType::WALL)], 0.9);
}

/**
 * Test that gatherMaterialHistograms handles edge of world correctly.
 */
TEST(SensoryUtilsTest, GatherHistogramsAtWorldEdge)
{
    // Create 10x10 world.
    auto world = std::make_unique<World>(10, 10);

    // Clear to air and add wall border.
    for (uint32_t y = 0; y < 10; y++) {
        for (uint32_t x = 0; x < 10; x++) {
            if (x == 0 || x == 9 || y == 0 || y == 9) {
                world->addMaterialAtCell(x, y, MaterialType::WALL, 1.0);
            }
            else {
                world->getData().at(x, y) = Cell();
            }
        }
    }

    // Request 9x9 grid centered at (8,5) - near right edge, extends to (12,5) which is OOB.
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};
    Vector2i world_offset;
    SensoryUtils::gatherMaterialHistograms<9, 10>(*world, Vector2i{ 8, 5 }, histograms, world_offset);

    // Offset is centered on organism: (8-4, 5-4) = (4, 1).
    EXPECT_EQ(world_offset.x, 4);
    EXPECT_EQ(world_offset.y, 1);

    // Right edge of grid (neural [4][8]) maps to world (12,5) which is OOB, should be WALL.
    EXPECT_GT(histograms[4][8][static_cast<int>(MaterialType::WALL)], 0.9);

    // Also the actual wall at world x=9 (neural x=5) should still be visible.
    EXPECT_GT(histograms[4][5][static_cast<int>(MaterialType::WALL)], 0.9);
}

// =============================================================================
// SensoryUtils::getDominantMaterial tests
// =============================================================================

/**
 * Test getDominantMaterial returns the material with highest fill.
 */
TEST(SensoryUtilsTest, GetDominantMaterialReturnsHighestFill)
{
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};

    // Set up a cell with multiple materials - SAND has highest fill.
    histograms[4][4][static_cast<int>(MaterialType::DIRT)] = 0.3;
    histograms[4][4][static_cast<int>(MaterialType::SAND)] = 0.7;
    histograms[4][4][static_cast<int>(MaterialType::WATER)] = 0.1;

    MaterialType dominant = SensoryUtils::getDominantMaterial<9, 10>(histograms, 4, 4);
    EXPECT_EQ(dominant, MaterialType::SAND);
}

/**
 * Test getDominantMaterial returns AIR for empty cells.
 */
TEST(SensoryUtilsTest, GetDominantMaterialReturnsAirForEmpty)
{
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};

    // All zeros - should return AIR (index 0).
    MaterialType dominant = SensoryUtils::getDominantMaterial<9, 10>(histograms, 4, 4);
    EXPECT_EQ(dominant, MaterialType::AIR);
}

/**
 * Test getDominantMaterial returns AIR for out-of-bounds coordinates.
 */
TEST(SensoryUtilsTest, GetDominantMaterialReturnsAirForOOB)
{
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};

    // Out of bounds should return AIR.
    MaterialType result1 = SensoryUtils::getDominantMaterial<9, 10>(histograms, -1, 4);
    EXPECT_EQ(result1, MaterialType::AIR);

    MaterialType result2 = SensoryUtils::getDominantMaterial<9, 10>(histograms, 9, 4);
    EXPECT_EQ(result2, MaterialType::AIR);

    MaterialType result3 = SensoryUtils::getDominantMaterial<9, 10>(histograms, 4, -1);
    EXPECT_EQ(result3, MaterialType::AIR);

    MaterialType result4 = SensoryUtils::getDominantMaterial<9, 10>(histograms, 4, 9);
    EXPECT_EQ(result4, MaterialType::AIR);
}

// =============================================================================
// SensoryUtils::isSolid tests
// =============================================================================

/**
 * Test isSolid returns true for solid materials.
 */
TEST(SensoryUtilsTest, IsSolidReturnsTrueForSolidMaterials)
{
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};

    // DIRT is solid.
    histograms[0][0][static_cast<int>(MaterialType::DIRT)] = 1.0;
    bool result1 = SensoryUtils::isSolid<9, 10>(histograms, 0, 0);
    EXPECT_TRUE(result1);

    // SAND is solid.
    histograms[1][0][static_cast<int>(MaterialType::SAND)] = 1.0;
    bool result2 = SensoryUtils::isSolid<9, 10>(histograms, 0, 1);
    EXPECT_TRUE(result2);

    // WOOD is solid.
    histograms[2][0][static_cast<int>(MaterialType::WOOD)] = 1.0;
    bool result3 = SensoryUtils::isSolid<9, 10>(histograms, 0, 2);
    EXPECT_TRUE(result3);

    // METAL is solid.
    histograms[3][0][static_cast<int>(MaterialType::METAL)] = 1.0;
    bool result4 = SensoryUtils::isSolid<9, 10>(histograms, 0, 3);
    EXPECT_TRUE(result4);

    // WALL is solid.
    histograms[4][0][static_cast<int>(MaterialType::WALL)] = 1.0;
    bool result5 = SensoryUtils::isSolid<9, 10>(histograms, 0, 4);
    EXPECT_TRUE(result5);
}

/**
 * Test isSolid returns false for non-solid materials.
 */
TEST(SensoryUtilsTest, IsSolidReturnsFalseForNonSolidMaterials)
{
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};

    // AIR is not solid.
    histograms[0][0][static_cast<int>(MaterialType::AIR)] = 1.0;
    bool result1 = SensoryUtils::isSolid<9, 10>(histograms, 0, 0);
    EXPECT_FALSE(result1);

    // WATER is not solid.
    histograms[1][0][static_cast<int>(MaterialType::WATER)] = 1.0;
    bool result2 = SensoryUtils::isSolid<9, 10>(histograms, 0, 1);
    EXPECT_FALSE(result2);

    // Empty cell is not solid.
    bool result3 = SensoryUtils::isSolid<9, 10>(histograms, 5, 5);
    EXPECT_FALSE(result3);
}

// =============================================================================
// SensoryUtils::isEmpty tests
// =============================================================================

/**
 * Test isEmpty returns true for cells with very low fill.
 */
TEST(SensoryUtilsTest, IsEmptyReturnsTrueForLowFillCells)
{
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};

    // Completely empty.
    bool result1 = SensoryUtils::isEmpty<9, 10>(histograms, 4, 4);
    EXPECT_TRUE(result1);

    // Very low fill (below 0.1 threshold).
    histograms[5][5][static_cast<int>(MaterialType::DIRT)] = 0.05;
    bool result2 = SensoryUtils::isEmpty<9, 10>(histograms, 5, 5);
    EXPECT_TRUE(result2);
}

/**
 * Test isEmpty returns false for cells with significant fill.
 */
TEST(SensoryUtilsTest, IsEmptyReturnsFalseForFilledCells)
{
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};

    // Above threshold.
    histograms[4][4][static_cast<int>(MaterialType::DIRT)] = 0.5;
    bool result1 = SensoryUtils::isEmpty<9, 10>(histograms, 4, 4);
    EXPECT_FALSE(result1);

    // Multiple materials adding up.
    histograms[5][5][static_cast<int>(MaterialType::DIRT)] = 0.05;
    histograms[5][5][static_cast<int>(MaterialType::SAND)] = 0.06;
    bool result2 = SensoryUtils::isEmpty<9, 10>(histograms, 5, 5);
    EXPECT_FALSE(result2);
}

/**
 * Test isEmpty returns true for out-of-bounds coordinates.
 */
TEST(SensoryUtilsTest, IsEmptyReturnsTrueForOOB)
{
    std::array<std::array<std::array<double, 10>, 9>, 9> histograms = {};

    bool result1 = SensoryUtils::isEmpty<9, 10>(histograms, -1, 4);
    EXPECT_TRUE(result1);

    bool result2 = SensoryUtils::isEmpty<9, 10>(histograms, 9, 4);
    EXPECT_TRUE(result2);

    bool result3 = SensoryUtils::isEmpty<9, 10>(histograms, 4, -1);
    EXPECT_TRUE(result3);

    bool result4 = SensoryUtils::isEmpty<9, 10>(histograms, 4, 9);
    EXPECT_TRUE(result4);
}

// =============================================================================
// Duck::gatherSensoryData tests
// =============================================================================

/**
 * Test that Duck::gatherSensoryData returns correct position and state.
 */
TEST(DuckSensoryDataTest, GatherSensoryDataReturnsCorrectPositionAndState)
{
    // Create 15x15 world with floor.
    auto world = std::make_unique<World>(15, 15);
    for (uint32_t y = 0; y < 15; y++) {
        for (uint32_t x = 0; x < 15; x++) {
            if (y == 14) {
                world->addMaterialAtCell(x, y, MaterialType::WALL, 1.0);
            }
            else {
                world->getData().at(x, y) = Cell();
            }
        }
    }

    // Create duck at (7, 12).
    OrganismId duck_id = world->getOrganismManager().createDuck(*world, 7, 12);
    Duck* duck = world->getOrganismManager().getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Let duck settle onto floor.
    for (int i = 0; i < 50; i++) {
        world->advanceTime(0.016);
    }

    // Gather sensory data.
    DuckSensoryData sensory = duck->gatherSensoryData(*world);

    // Position should match anchor cell.
    EXPECT_EQ(sensory.position.x, duck->getAnchorCell().x);
    EXPECT_EQ(sensory.position.y, duck->getAnchorCell().y);

    // Should be on ground (settled on floor).
    EXPECT_TRUE(sensory.on_ground);

    // Grid size should be 9x9.
    EXPECT_EQ(sensory.actual_width, DuckSensoryData::GRID_SIZE);
    EXPECT_EQ(sensory.actual_height, DuckSensoryData::GRID_SIZE);
    EXPECT_EQ(DuckSensoryData::GRID_SIZE, 9);
}

/**
 * Test that Duck::gatherSensoryData correctly samples environment.
 */
TEST(DuckSensoryDataTest, GatherSensoryDataSamplesEnvironment)
{
    // Create 15x15 world.
    auto world = std::make_unique<World>(15, 15);
    for (uint32_t y = 0; y < 15; y++) {
        for (uint32_t x = 0; x < 15; x++) {
            world->getData().at(x, y) = Cell();
        }
    }

    // Add floor at y=13.
    for (uint32_t x = 0; x < 15; x++) {
        world->addMaterialAtCell(x, 13, MaterialType::DIRT, 1.0);
    }

    // Add wall to the right at x=10.
    for (uint32_t y = 0; y < 13; y++) {
        world->addMaterialAtCell(10, y, MaterialType::WALL, 1.0);
    }

    // Add water pool to the left.
    world->addMaterialAtCell(4, 12, MaterialType::WATER, 1.0);

    // Create duck at (7, 12).
    OrganismId duck_id = world->getOrganismManager().createDuck(*world, 7, 12);
    Duck* duck = world->getOrganismManager().getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    // Gather sensory data immediately (before physics moves duck).
    DuckSensoryData sensory = duck->gatherSensoryData(*world);

    // Duck at (7,12), 9x9 grid centered on duck.
    // Offset = (7-4, 12-4) = (3, 8). No clamping - out-of-bounds treated as WALL.
    EXPECT_EQ(sensory.world_offset.x, 3);
    EXPECT_EQ(sensory.world_offset.y, 8);

    // Floor at y=13 should be at neural y = 13 - 8 = 5.
    // Check that row 5 has DIRT.
    bool found_dirt = false;
    for (int x = 0; x < DuckSensoryData::GRID_SIZE; x++) {
        if (sensory.material_histograms[5][x][static_cast<int>(MaterialType::DIRT)] > 0.5) {
            found_dirt = true;
            break;
        }
    }
    EXPECT_TRUE(found_dirt) << "Should see DIRT floor in sensory grid";

    // Wall at x=10 should be at neural x = 10 - 3 = 7.
    bool found_wall = false;
    for (int y = 0; y < DuckSensoryData::GRID_SIZE; y++) {
        if (sensory.material_histograms[y][7][static_cast<int>(MaterialType::WALL)] > 0.5) {
            found_wall = true;
            break;
        }
    }
    EXPECT_TRUE(found_wall) << "Should see WALL to the right in sensory grid";

    // Water at (4,12) should be at neural (4-3, 12-8) = (1, 4).
    EXPECT_GT(sensory.material_histograms[4][1][static_cast<int>(MaterialType::WATER)], 0.5)
        << "Should see WATER to the left in sensory grid";
}

/**
 * Test that sensory data detects wall ahead of duck.
 */
TEST(DuckSensoryDataTest, SensoryDataDetectsWallAhead)
{
    // Create 15x15 world.
    auto world = std::make_unique<World>(15, 15);
    for (uint32_t y = 0; y < 15; y++) {
        for (uint32_t x = 0; x < 15; x++) {
            world->getData().at(x, y) = Cell();
        }
    }

    // Add floor.
    for (uint32_t x = 0; x < 15; x++) {
        world->addMaterialAtCell(x, 13, MaterialType::DIRT, 1.0);
    }

    // Add wall 2 cells to the right of where duck will be.
    world->addMaterialAtCell(9, 12, MaterialType::WALL, 1.0);

    // Create duck at (7, 12) facing right.
    OrganismId duck_id = world->getOrganismManager().createDuck(*world, 7, 12);
    Duck* duck = world->getOrganismManager().getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    DuckSensoryData sensory = duck->gatherSensoryData(*world);

    // Duck at (7,12), wall at (9,12).
    // Neural coords: duck center at (4,4), wall at (9-3, 12-8) = (6, 4).
    // So wall is 2 cells to the right of center in neural grid.

    int wall_neural_x = 9 - sensory.world_offset.x;
    int wall_neural_y = 12 - sensory.world_offset.y;

    ASSERT_GE(wall_neural_x, 0);
    ASSERT_LT(wall_neural_x, DuckSensoryData::GRID_SIZE);
    ASSERT_GE(wall_neural_y, 0);
    ASSERT_LT(wall_neural_y, DuckSensoryData::GRID_SIZE);

    // Check that isSolid detects the wall.
    bool wall_is_solid = SensoryUtils::isSolid<DuckSensoryData::GRID_SIZE, DuckSensoryData::NUM_MATERIALS>(
        sensory.material_histograms, wall_neural_x, wall_neural_y);
    EXPECT_TRUE(wall_is_solid) << "Should detect wall ahead as solid";

    // Center should not be solid (it's the duck's WOOD cell, but let's check air around it).
    // Actually the duck itself is WOOD which is solid, so check one cell away.
    int left_of_duck_x = 4 - 1; // One cell left of center in neural coords.
    bool left_is_solid = SensoryUtils::isSolid<DuckSensoryData::GRID_SIZE, DuckSensoryData::NUM_MATERIALS>(
        sensory.material_histograms, left_of_duck_x, 4);
    EXPECT_FALSE(left_is_solid) << "Air to the left of duck should not be solid";
}
