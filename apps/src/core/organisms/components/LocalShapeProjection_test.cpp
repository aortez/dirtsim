/**
 * Tests for LocalShapeProjection.
 *
 * Verifies grid projection, cell clearing, and growth/removal for
 * multi-cell rigid body organisms.
 */

#include "LocalShapeProjection.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>

using namespace DirtSim;

class LocalShapeProjectionTest : public ::testing::Test {
protected:
    std::unique_ptr<World> createWorld(uint32_t width = 10, uint32_t height = 10)
    {
        return std::make_unique<World>(width, height);
    }

    OrganismId createTestOrganism(World& world, int x, int y)
    {
        return world.getOrganismManager().createGoose(world, x, y);
    }
};

// =============================================================================
// Projection
// =============================================================================

TEST_F(LocalShapeProjectionTest, ProjectsSingleCellToGrid)
{
    auto world = createWorld();
    OrganismId id = createTestOrganism(*world, 5, 5);
    LocalShapeProjection projection;

    projection.addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);
    projection.project(*world, id, { 5.5, 5.5 }, { 0.0, 0.0 });

    // Cell should be at grid position (5, 5).
    const auto& cell = world->getData().at(5, 5);
    EXPECT_EQ(cell.material_type, Material::EnumType::Wood);
    EXPECT_NEAR(cell.fill_ratio, 1.0, 0.0001);
    EXPECT_EQ(world->getOrganismManager().at({ 5, 5 }), id);
}

TEST_F(LocalShapeProjectionTest, ProjectsMultipleCellsToGrid)
{
    auto world = createWorld();
    OrganismId id = createTestOrganism(*world, 3, 3);
    LocalShapeProjection projection;

    // L-shaped organism.
    projection.addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);
    projection.addCell({ 1, 0 }, Material::EnumType::Wood, 1.0);
    projection.addCell({ 0, 1 }, Material::EnumType::Wood, 1.0);

    projection.project(*world, id, { 3.0, 3.0 }, { 0.0, 0.0 });

    EXPECT_EQ(world->getData().at(3, 3).material_type, Material::EnumType::Wood);
    EXPECT_EQ(world->getData().at(4, 3).material_type, Material::EnumType::Wood);
    EXPECT_EQ(world->getData().at(3, 4).material_type, Material::EnumType::Wood);
}

TEST_F(LocalShapeProjectionTest, SetsVelocityOnProjectedCells)
{
    auto world = createWorld();
    OrganismId id = createTestOrganism(*world, 5, 5);
    LocalShapeProjection projection;

    projection.addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);
    projection.project(*world, id, { 5.0, 5.0 }, { 2.5, -1.0 });

    const auto& cell = world->getData().at(5, 5);
    EXPECT_NEAR(cell.velocity.x, 2.5, 0.0001);
    EXPECT_NEAR(cell.velocity.y, -1.0, 0.0001);
}

TEST_F(LocalShapeProjectionTest, ComputesSubCellCOMFromFractionalPosition)
{
    auto world = createWorld();
    OrganismId id = createTestOrganism(*world, 5, 5);
    LocalShapeProjection projection;

    projection.addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);

    // Position at (5.75, 5.25) -> fractional (0.75, 0.25) -> COM (0.5, -0.5).
    projection.project(*world, id, { 5.75, 5.25 }, { 0.0, 0.0 });

    const auto& cell = world->getData().at(5, 5);
    EXPECT_NEAR(cell.com.x, 0.5, 0.0001);
    EXPECT_NEAR(cell.com.y, -0.5, 0.0001);
}

TEST_F(LocalShapeProjectionTest, TracksOccupiedCells)
{
    auto world = createWorld();
    OrganismId id = createTestOrganism(*world, 5, 5);
    LocalShapeProjection projection;

    projection.addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);
    projection.addCell({ 1, 0 }, Material::EnumType::Wood, 1.0);

    projection.project(*world, id, { 5.0, 5.0 }, { 0.0, 0.0 });

    const auto& occupied = projection.getOccupiedCells();
    EXPECT_EQ(occupied.size(), 2u);
}

// =============================================================================
// Clearing
// =============================================================================

TEST_F(LocalShapeProjectionTest, ClearsOldProjectionOnReproject)
{
    auto world = createWorld();
    OrganismId id = createTestOrganism(*world, 5, 5);
    LocalShapeProjection projection;

    projection.addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);

    // Project at (5, 5).
    projection.project(*world, id, { 5.0, 5.0 }, { 0.0, 0.0 });
    EXPECT_EQ(world->getData().at(5, 5).material_type, Material::EnumType::Wood);

    // Reproject at (7, 7).
    projection.project(*world, id, { 7.0, 7.0 }, { 0.0, 0.0 });

    // Old position should be cleared.
    EXPECT_EQ(world->getData().at(5, 5).material_type, Material::EnumType::Air);
    // New position should have the cell.
    EXPECT_EQ(world->getData().at(7, 7).material_type, Material::EnumType::Wood);
}

TEST_F(LocalShapeProjectionTest, ClearResetsOccupiedCells)
{
    auto world = createWorld();
    OrganismId id = createTestOrganism(*world, 5, 5);
    LocalShapeProjection projection;

    projection.addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);
    projection.project(*world, id, { 5.0, 5.0 }, { 0.0, 0.0 });

    EXPECT_EQ(projection.getOccupiedCells().size(), 1u);

    projection.clear(*world);

    EXPECT_EQ(projection.getOccupiedCells().size(), 0u);
}

// =============================================================================
// Growth
// =============================================================================

TEST_F(LocalShapeProjectionTest, AddCellExpandsShape)
{
    LocalShapeProjection projection;

    projection.addCell({ 0, 0 }, Material::EnumType::Seed, 1.0);
    EXPECT_EQ(projection.getLocalShape().size(), 1u);

    projection.addCell({ 0, 1 }, Material::EnumType::Root, 1.0);
    EXPECT_EQ(projection.getLocalShape().size(), 2u);
}

TEST_F(LocalShapeProjectionTest, RemoveCellShrinksShape)
{
    LocalShapeProjection projection;

    projection.addCell({ 0, 0 }, Material::EnumType::Seed, 1.0);
    projection.addCell({ 0, 1 }, Material::EnumType::Root, 1.0);
    EXPECT_EQ(projection.getLocalShape().size(), 2u);

    projection.removeCell({ 0, 1 });
    EXPECT_EQ(projection.getLocalShape().size(), 1u);

    // The SEED at (0,0) should still be there.
    EXPECT_EQ(projection.getLocalShape()[0].material, Material::EnumType::Seed);
}

TEST_F(LocalShapeProjectionTest, RemoveNonexistentCellDoesNothing)
{
    LocalShapeProjection projection;

    projection.addCell({ 0, 0 }, Material::EnumType::Seed, 1.0);
    EXPECT_EQ(projection.getLocalShape().size(), 1u);

    projection.removeCell({ 99, 99 });
    EXPECT_EQ(projection.getLocalShape().size(), 1u);
}
