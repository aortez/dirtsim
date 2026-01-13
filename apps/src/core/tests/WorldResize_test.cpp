/**
 * @file WorldResize_test.cpp
 * @brief Tests for World grid resizing behavior.
 *
 * Tests cover:
 * - Edge wall preservation during expanding and shrinking.
 * - Interior material interpolation.
 * - Organism repositioning at proportional locations.
 */

#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldInterpolationTool.h"
#include "core/organisms/Duck.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include <gtest/gtest.h>

using namespace DirtSim;

/**
 * @brief Test fixture for World resize tests.
 */
class WorldResizeTest : public ::testing::Test {
protected:
    /**
     * @brief Helper to create a world with WALL at all edges.
     */
    std::unique_ptr<World> createWorldWithWalls(uint32_t width, uint32_t height)
    {
        auto world = std::make_unique<World>(width, height);
        WorldData& data = world->getData();

        // Fill edges with WALL.
        for (uint32_t x = 0; x < width; ++x) {
            data.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
            data.at(x, height - 1).replaceMaterial(Material::EnumType::Wall, 1.0);
        }
        for (uint32_t y = 0; y < height; ++y) {
            data.at(0, y).replaceMaterial(Material::EnumType::Wall, 1.0);
            data.at(width - 1, y).replaceMaterial(Material::EnumType::Wall, 1.0);
        }

        return world;
    }

    /**
     * @brief Helper to fill a rectangular region with a material.
     */
    void fillRegion(
        World& world,
        uint32_t x1,
        uint32_t y1,
        uint32_t x2,
        uint32_t y2,
        Material::EnumType material)
    {
        WorldData& data = world.getData();
        for (uint32_t y = y1; y <= y2; ++y) {
            for (uint32_t x = x1; x <= x2; ++x) {
                data.at(x, y).replaceMaterial(material, 1.0);
            }
        }
    }
};

// =============================================================================
// Edge Wall Preservation Tests.
// =============================================================================

/**
 * @brief Test that edge walls are preserved when expanding the grid.
 *
 * A 10x10 world with WALL at edges should expand to 20x20 with WALL at edges.
 * Walls should scale thicker proportionally.
 */
TEST_F(WorldResizeTest, ResizePreservesEdgeWalls_Expanding)
{
    // Setup: 10x10 world with WALL at edges.
    auto world = createWorldWithWalls(10, 10);

    // Verify initial state.
    EXPECT_EQ(world->getData().at(0, 0).material_type, Material::EnumType::Wall);
    EXPECT_EQ(world->getData().at(9, 9).material_type, Material::EnumType::Wall);
    EXPECT_EQ(world->getData().at(5, 5).material_type, Material::EnumType::Air);

    // Action: Resize to 20x20.
    world->resizeGrid(20, 20);

    // Verify: Edges still have WALL.
    EXPECT_EQ(world->getData().width, 20);
    EXPECT_EQ(world->getData().height, 20);

    // Check all four corners.
    EXPECT_EQ(world->getData().at(0, 0).material_type, Material::EnumType::Wall)
        << "Top-left corner should be WALL";
    EXPECT_EQ(world->getData().at(19, 0).material_type, Material::EnumType::Wall)
        << "Top-right corner should be WALL";
    EXPECT_EQ(world->getData().at(0, 19).material_type, Material::EnumType::Wall)
        << "Bottom-left corner should be WALL";
    EXPECT_EQ(world->getData().at(19, 19).material_type, Material::EnumType::Wall)
        << "Bottom-right corner should be WALL";

    // Check edge cells (not just corners).
    EXPECT_EQ(world->getData().at(10, 0).material_type, Material::EnumType::Wall)
        << "Top edge middle should be WALL";
    EXPECT_EQ(world->getData().at(0, 10).material_type, Material::EnumType::Wall)
        << "Left edge middle should be WALL";
}

/**
 * @brief Test that edge walls are preserved when shrinking the grid.
 *
 * A 20x20 world with WALL at edges should shrink to 10x10 with WALL at edges.
 * This is the critical test - walls should not be diluted to interior material.
 */
TEST_F(WorldResizeTest, ResizePreservesEdgeWalls_Shrinking)
{
    // Setup: 20x20 world with WALL at edges, DIRT in interior.
    auto world = createWorldWithWalls(20, 20);
    fillRegion(*world, 1, 1, 18, 18, Material::EnumType::Dirt);

    // Verify initial state.
    EXPECT_EQ(world->getData().at(0, 0).material_type, Material::EnumType::Wall);
    EXPECT_EQ(world->getData().at(10, 10).material_type, Material::EnumType::Dirt);

    // Action: Resize to 10x10.
    world->resizeGrid(10, 10);

    // Verify: Edges still have WALL (not diluted to DIRT).
    EXPECT_EQ(world->getData().width, 10);
    EXPECT_EQ(world->getData().height, 10);

    // Check all four edges.
    EXPECT_EQ(world->getData().at(0, 0).material_type, Material::EnumType::Wall)
        << "Top-left corner should be WALL, not diluted";
    EXPECT_EQ(world->getData().at(9, 0).material_type, Material::EnumType::Wall)
        << "Top-right corner should be WALL, not diluted";
    EXPECT_EQ(world->getData().at(0, 9).material_type, Material::EnumType::Wall)
        << "Bottom-left corner should be WALL, not diluted";
    EXPECT_EQ(world->getData().at(9, 9).material_type, Material::EnumType::Wall)
        << "Bottom-right corner should be WALL, not diluted";

    // Interior should still be DIRT.
    EXPECT_EQ(world->getData().at(5, 5).material_type, Material::EnumType::Dirt)
        << "Interior should be DIRT";
}

// =============================================================================
// Interior Material Tests.
// =============================================================================

/**
 * @brief Test that interior material blobs scale proportionally.
 *
 * A DIRT blob in the center of a 10x10 world should remain centered after
 * resizing to 20x20, and be proportionally larger.
 */
TEST_F(WorldResizeTest, ResizePreservesInteriorMaterial)
{
    // Setup: 10x10 world with a 2x2 DIRT blob in center (at 4,4 to 5,5).
    auto world = std::make_unique<World>(10, 10);
    fillRegion(*world, 4, 4, 5, 5, Material::EnumType::Dirt);

    // Verify initial state.
    EXPECT_EQ(world->getData().at(4, 4).material_type, Material::EnumType::Dirt);
    EXPECT_EQ(world->getData().at(5, 5).material_type, Material::EnumType::Dirt);

    // Action: Resize to 20x20.
    world->resizeGrid(20, 20);

    // Verify: DIRT should still be in the center region.
    // Original (4,4)-(5,5) maps to approximately (8,8)-(11,11) in 20x20.
    EXPECT_EQ(world->getData().at(9, 9).material_type, Material::EnumType::Dirt)
        << "Center should still be DIRT";
    EXPECT_EQ(world->getData().at(10, 10).material_type, Material::EnumType::Dirt)
        << "Center should still be DIRT";
}

// =============================================================================
// Organism Repositioning Tests.
// =============================================================================

/**
 * @brief Test that incremental resizing does not cause organism drift.
 *
 * Reproduces the real-world bug: duck at bottom-right of non-square world,
 * incrementally expand then shrink. Duck should maintain relative position
 * within tolerance, not drift toward top-left due to integer truncation.
 */
TEST_F(WorldResizeTest, IncrementalResizingDoesNotCauseDrift)
{
    // Setup: Duck at bottom-right of a non-square world (like sandbox).
    auto world = std::make_unique<World>(45, 30);
    OrganismId duckId = world->getOrganismManager().createDuck(*world, 31, 20);

    Vector2i initialAnchor{ 31, 20 };
    auto* duck = world->getOrganismManager().getDuck(duckId);
    ASSERT_NE(duck, nullptr);
    EXPECT_EQ(duck->getAnchorCell(), initialAnchor);

    // Calculate initial relative position.
    double initialRelX = static_cast<double>(initialAnchor.x) / 45.0;
    double initialRelY = static_cast<double>(initialAnchor.y) / 30.0;

    // Incrementally expand: 45x30 → 60x45.
    for (uint32_t w = 46; w <= 60; ++w) {
        uint32_t h = std::min(30 + (w - 45), 45u);
        world->resizeGrid(w, h);

        duck = world->getOrganismManager().getDuck(duckId);
        ASSERT_NE(duck, nullptr) << "Duck lost during expansion at " << w << "x" << h;

        Vector2i anchor = duck->getAnchorCell();
        double expectedX = initialRelX * w;
        double expectedY = initialRelY * h;

        EXPECT_NEAR(anchor.x, expectedX, 1.0)
            << "Duck drifted during expansion to " << w << "x" << h;
        EXPECT_NEAR(anchor.y, expectedY, 1.0)
            << "Duck drifted during expansion to " << w << "x" << h;
    }

    // Incrementally shrink back: 60x45 → 45x30.
    for (int w = 59; w >= 45; --w) {
        uint32_t h = std::max(30 + (w - 45), 30);
        world->resizeGrid(w, h);

        duck = world->getOrganismManager().getDuck(duckId);
        ASSERT_NE(duck, nullptr) << "Duck lost during shrinking at " << w << "x" << h;

        Vector2i anchor = duck->getAnchorCell();
        double expectedX = initialRelX * w;
        double expectedY = initialRelY * h;

        EXPECT_NEAR(anchor.x, expectedX, 1.0)
            << "Duck drifted during shrinking to " << w << "x" << h;
        EXPECT_NEAR(anchor.y, expectedY, 1.0)
            << "Duck drifted during shrinking to " << w << "x" << h;
    }

    // Final check: Duck should be back at original position (or within ±1).
    duck = world->getOrganismManager().getDuck(duckId);
    ASSERT_NE(duck, nullptr);
    Vector2i finalAnchor = duck->getAnchorCell();

    EXPECT_NEAR(finalAnchor.x, initialAnchor.x, 1);
    EXPECT_NEAR(finalAnchor.y, initialAnchor.y, 1);
}
