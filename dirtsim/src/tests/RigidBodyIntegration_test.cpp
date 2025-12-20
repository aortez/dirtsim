#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace DirtSim;

class RigidBodyIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::info); }

    std::unique_ptr<World> createWorld(uint32_t width, uint32_t height)
    {
        auto world = std::make_unique<World>(width, height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
            }
        }
        return world;
    }
};

TEST_F(RigidBodyIntegrationTest, FloatingStructureFallsTogether)
{
    auto world = createWorld(10, 10);
    OrganismManager& organism_manager = world->getOrganismManager();

    // Plant seed at (4, 3) - this creates the tree.
    OrganismId tree_id = organism_manager.createTree(*world, 4, 3);

    // Build 2x2 structure by adding adjacent WOOD cells.
    world->getData().at(5, 3).replaceMaterial(MaterialType::WOOD, 1.0);
    organism_manager.addCellToOrganism(*world, tree_id, { 5, 3 });
    world->getData().at(4, 4).replaceMaterial(MaterialType::WOOD, 1.0);
    organism_manager.addCellToOrganism(*world, tree_id, { 4, 4 });
    world->getData().at(5, 4).replaceMaterial(MaterialType::WOOD, 1.0);
    organism_manager.addCellToOrganism(*world, tree_id, { 5, 4 });

    // Run physics for several frames.
    for (int frame = 0; frame < 20; ++frame) {
        world->advanceTime(0.016);

        // Verify all cells have same velocity.
        const Cell& cell1 = world->getData().at(4, 3);
        const Cell& cell2 = world->getData().at(5, 3);
        const Cell& cell3 = world->getData().at(4, 4);
        const Cell& cell4 = world->getData().at(5, 4);

        EXPECT_NEAR(cell1.velocity.x, cell2.velocity.x, 0.0001)
            << "Frame " << frame << ": cells have different X velocities";
        EXPECT_NEAR(cell1.velocity.x, cell3.velocity.x, 0.0001);
        EXPECT_NEAR(cell1.velocity.x, cell4.velocity.x, 0.0001);

        EXPECT_NEAR(cell1.velocity.y, cell2.velocity.y, 0.0001)
            << "Frame " << frame << ": cells have different Y velocities";
        EXPECT_NEAR(cell1.velocity.y, cell3.velocity.y, 0.0001);
        EXPECT_NEAR(cell1.velocity.y, cell4.velocity.y, 0.0001);

        // Structure should be falling (positive Y velocity).
        if (frame > 5) { // Give it a few frames to accelerate.
            EXPECT_GT(cell1.velocity.y, 0.1) << "Frame " << frame << ": structure not falling";
        }
    }
}

TEST_F(RigidBodyIntegrationTest, TreeStructureMovesAsUnit)
{
    auto world = createWorld(6, 4);
    OrganismManager& organism_manager = world->getOrganismManager();

    // Simple tree floating in air: SEED-WOOD horizontal.
    OrganismId tree_id = organism_manager.createTree(*world, 1, 1);
    world->getData().at(2, 1).replaceMaterial(MaterialType::WOOD, 1.0);
    organism_manager.addCellToOrganism(*world, tree_id, { 2, 1 });

    // Verify setup.
    EXPECT_EQ(world->getData().at(1, 1).material_type, MaterialType::SEED);
    EXPECT_EQ(world->getData().at(1, 1).organism_id, tree_id);
    EXPECT_EQ(world->getData().at(2, 1).material_type, MaterialType::WOOD);
    EXPECT_EQ(world->getData().at(2, 1).organism_id, tree_id);

    // Run several physics frames.
    for (int frame = 0; frame < 10; ++frame) {
        world->advanceTime(0.016);
    }

    // Verify tree cells have same velocity.
    const Cell& seed = world->getData().at(1, 1);
    const Cell& wood = world->getData().at(2, 1);

    EXPECT_NEAR(seed.velocity.x, wood.velocity.x, 0.001)
        << "Tree cells have different X velocities";
    EXPECT_NEAR(seed.velocity.y, wood.velocity.y, 0.001)
        << "Tree cells have different Y velocities";
}

TEST_F(RigidBodyIntegrationTest, MultipleStructuresMoveIndependently)
{
    auto world = createWorld(10, 10);
    OrganismManager& organism_manager = world->getOrganismManager();

    // Create two separate tree structures.
    // Structure 1: seed + WOOD at y=3.
    OrganismId tree1 = organism_manager.createTree(*world, 2, 3);
    world->getData().at(3, 3).replaceMaterial(MaterialType::WOOD, 1.0);
    organism_manager.addCellToOrganism(*world, tree1, { 3, 3 });

    // Structure 2: seed + WOOD at y=6.
    OrganismId tree2 = organism_manager.createTree(*world, 6, 6);
    world->getData().at(7, 6).replaceMaterial(MaterialType::WOOD, 1.0);
    organism_manager.addCellToOrganism(*world, tree2, { 7, 6 });

    // Run physics.
    for (int frame = 0; frame < 10; ++frame) {
        world->advanceTime(0.016);
    }

    // Each structure should have unified velocity within itself.
    const Cell& seed1 = world->getData().at(2, 3);
    const Cell& wood1 = world->getData().at(3, 3);
    EXPECT_NEAR(seed1.velocity.x, wood1.velocity.x, 0.0001);
    EXPECT_NEAR(seed1.velocity.y, wood1.velocity.y, 0.0001);

    const Cell& seed2 = world->getData().at(6, 6);
    const Cell& wood2 = world->getData().at(7, 6);
    EXPECT_NEAR(seed2.velocity.x, wood2.velocity.x, 0.0001);
    EXPECT_NEAR(seed2.velocity.y, wood2.velocity.y, 0.0001);

    // Both structures should be falling.
    EXPECT_GT(seed1.velocity.y, 0.1);
    EXPECT_GT(seed2.velocity.y, 0.1);
}

TEST_F(RigidBodyIntegrationTest, DisconnectedFragmentGetsPruned)
{
    auto world = createWorld(10, 5);
    OrganismManager& organism_manager = world->getOrganismManager();

    // Plant seed at (2, 2).
    OrganismId tree_id = organism_manager.createTree(*world, 2, 2);

    // Build a tree structure: SEED-WOOD-WOOD connected, then a gap, then disconnected WOOD.
    // Layout:  [SEED]-[WOOD]-[WOOD]   [WOOD]  (gap at x=5, disconnected WOOD at x=6)
    //          (2,2)  (3,2)  (4,2)    (6,2)

    // Add connected WOOD cells.
    world->getData().at(3, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    organism_manager.addCellToOrganism(*world, tree_id, { 3, 2 });

    world->getData().at(4, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    organism_manager.addCellToOrganism(*world, tree_id, { 4, 2 });

    // Add disconnected WOOD cell (gap at x=5).
    world->getData().at(6, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    organism_manager.addCellToOrganism(*world, tree_id, { 6, 2 });

    // Verify initial state.
    EXPECT_EQ(world->getData().at(2, 2).organism_id, tree_id); // SEED.
    EXPECT_EQ(world->getData().at(3, 2).organism_id, tree_id); // Connected WOOD.
    EXPECT_EQ(world->getData().at(4, 2).organism_id, tree_id); // Connected WOOD.
    EXPECT_EQ(world->getData().at(6, 2).organism_id, tree_id); // Disconnected WOOD.

    // Run one physics frame.
    world->advanceTime(0.016);

    // Verify connected cells still belong to organism.
    EXPECT_EQ(world->getData().at(2, 2).organism_id, tree_id) << "SEED should remain connected";
    EXPECT_EQ(world->getData().at(3, 2).organism_id, tree_id) << "Adjacent WOOD should remain connected";
    EXPECT_EQ(world->getData().at(4, 2).organism_id, tree_id) << "Adjacent WOOD should remain connected";

    // Verify disconnected cell was pruned.
    EXPECT_EQ(world->getData().at(6, 2).organism_id, 0u)
        << "Disconnected WOOD should have organism_id=0 after pruning";

    // Verify tree's cell tracking was updated.
    const Tree* tree = organism_manager.getTree(tree_id);
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->getCells().size(), 3u) << "Tree should track 3 cells (SEED + 2 WOOD)";
    EXPECT_TRUE(tree->getCells().count({ 2, 2 })) << "SEED should be in tree.cells";
    EXPECT_TRUE(tree->getCells().count({ 3, 2 })) << "Connected WOOD should be in tree.cells";
    EXPECT_TRUE(tree->getCells().count({ 4, 2 })) << "Connected WOOD should be in tree.cells";
    EXPECT_FALSE(tree->getCells().count({ 6, 2 })) << "Disconnected WOOD should NOT be in tree.cells";
}
