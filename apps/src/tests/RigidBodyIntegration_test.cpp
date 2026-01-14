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
                world->getData().at(x, y).replaceMaterial(Material::EnumType::Air, 0.0);
            }
        }
        return world;
    }
};

TEST_F(RigidBodyIntegrationTest, FloatingStructureFallsTogether)
{
    auto world = createWorld(10, 10);
    OrganismManager& organism_manager = world->getOrganismManager();

    // Plant seed at (4, 3) - this creates the tree with RigidBodyComponent.
    OrganismId tree_id = organism_manager.createTree(*world, 4, 3);
    Tree* tree = organism_manager.getTree(tree_id);
    ASSERT_NE(tree, nullptr);

    // Build 2x2 structure by adding adjacent WOOD cells to tree's local shape.
    // Local coords are relative to seed position: (4, 3) = local (0, 0).
    // (5, 3) = local (1, 0), (4, 4) = local (0, 1), (5, 4) = local (1, 1).
    tree->addCellToLocalShape({ 1, 0 }, Material::EnumType::Wood, 1.0);
    tree->addCellToLocalShape({ 0, 1 }, Material::EnumType::Wood, 1.0);
    tree->addCellToLocalShape({ 1, 1 }, Material::EnumType::Wood, 1.0);

    // Run one frame to project the local shape to the world grid.
    // (Use small deltaTime; 0.0 returns early without doing anything.)
    world->advanceTime(0.001);

    // Run physics for several frames.
    for (int frame = 0; frame < 20; ++frame) {
        world->advanceTime(0.016);

        // Get tree's actual cell positions (they move as tree falls).
        const auto& cells = tree->getCells();
        ASSERT_EQ(cells.size(), 4u) << "Frame " << frame << ": expected 4 cells in tree";

        // Collect velocities from all tree cells.
        std::vector<Vector2d> velocities;
        for (const auto& pos : cells) {
            const Cell& cell = world->getData().at(pos.x, pos.y);
            velocities.push_back(cell.velocity);
        }

        // Verify all cells have same velocity (rigid body behavior).
        Vector2d first_velocity = velocities[0];
        for (size_t i = 1; i < velocities.size(); ++i) {
            EXPECT_NEAR(first_velocity.x, velocities[i].x, 0.0001)
                << "Frame " << frame << ": cells have different X velocities";
            EXPECT_NEAR(first_velocity.y, velocities[i].y, 0.0001)
                << "Frame " << frame << ": cells have different Y velocities";
        }

        // Structure should be falling (positive Y velocity).
        if (frame > 5) { // Give it a few frames to accelerate.
            EXPECT_GT(first_velocity.y, 0.1) << "Frame " << frame << ": structure not falling";
        }
    }
}

TEST_F(RigidBodyIntegrationTest, TreeStructureMovesAsUnit)
{
    auto world = createWorld(6, 4);
    OrganismManager& organism_manager = world->getOrganismManager();

    // Simple tree floating in air: SEED-WOOD horizontal.
    OrganismId tree_id = organism_manager.createTree(*world, 1, 1);
    Tree* tree = organism_manager.getTree(tree_id);
    ASSERT_NE(tree, nullptr);

    // Add WOOD cell at local (1, 0) = world (2, 1).
    tree->addCellToLocalShape({ 1, 0 }, Material::EnumType::Wood, 1.0);

    // Run one frame to project cells (use small deltaTime; 0.0 returns early).
    world->advanceTime(0.001);

    // Verify setup.
    EXPECT_EQ(tree->getCells().size(), 2u);

    // Run several physics frames.
    for (int frame = 0; frame < 10; ++frame) {
        world->advanceTime(0.016);
    }

    // Verify tree cells have same velocity (check actual tree cells, not fixed positions).
    const auto& cells = tree->getCells();
    ASSERT_EQ(cells.size(), 2u);

    std::vector<Vector2d> velocities;
    for (const auto& pos : cells) {
        velocities.push_back(world->getData().at(pos.x, pos.y).velocity);
    }

    EXPECT_NEAR(velocities[0].x, velocities[1].x, 0.001)
        << "Tree cells have different X velocities";
    EXPECT_NEAR(velocities[0].y, velocities[1].y, 0.001)
        << "Tree cells have different Y velocities";
}

TEST_F(RigidBodyIntegrationTest, MultipleStructuresMoveIndependently)
{
    auto world = createWorld(10, 10);
    OrganismManager& organism_manager = world->getOrganismManager();

    // Create two separate tree structures.
    // Structure 1: seed + WOOD at y=3.
    OrganismId tree1_id = organism_manager.createTree(*world, 2, 3);
    Tree* tree1 = organism_manager.getTree(tree1_id);
    ASSERT_NE(tree1, nullptr);
    tree1->addCellToLocalShape({ 1, 0 }, Material::EnumType::Wood, 1.0);

    // Structure 2: seed + WOOD at y=6.
    OrganismId tree2_id = organism_manager.createTree(*world, 6, 6);
    Tree* tree2 = organism_manager.getTree(tree2_id);
    ASSERT_NE(tree2, nullptr);
    tree2->addCellToLocalShape({ 1, 0 }, Material::EnumType::Wood, 1.0);

    // Project cells (use small deltaTime; 0.0 returns early).
    world->advanceTime(0.001);

    // Run physics.
    for (int frame = 0; frame < 10; ++frame) {
        world->advanceTime(0.016);
    }

    // Each structure should have unified velocity within itself.
    auto checkUnifiedVelocity = [&](Tree* tree, const std::string& name) {
        const auto& cells = tree->getCells();
        ASSERT_EQ(cells.size(), 2u) << name << " should have 2 cells";

        std::vector<Vector2d> velocities;
        for (const auto& pos : cells) {
            velocities.push_back(world->getData().at(pos.x, pos.y).velocity);
        }

        EXPECT_NEAR(velocities[0].x, velocities[1].x, 0.0001)
            << name << " cells have different X velocities";
        EXPECT_NEAR(velocities[0].y, velocities[1].y, 0.0001)
            << name << " cells have different Y velocities";

        // Structure should be falling.
        EXPECT_GT(velocities[0].y, 0.1) << name << " is not falling";
    };

    checkUnifiedVelocity(tree1, "Tree1");
    checkUnifiedVelocity(tree2, "Tree2");
}
