#include "core/World.h"
#include "core/WorldData.h"
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

    // Create 2x2 wood structure (organism) floating in air.
    world->getData().at(4, 3).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(4, 3).organism_id = 1;
    world->getData().at(5, 3).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(5, 3).organism_id = 1;
    world->getData().at(4, 4).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(4, 4).organism_id = 1;
    world->getData().at(5, 4).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(5, 4).organism_id = 1;

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

TEST_F(RigidBodyIntegrationTest, WoodStructureInWaterMovesAsUnit)
{
    auto world = createWorld(4, 3);

    // Setup:
    // Row 0: AIR    AIR   AIR   AIR
    // Row 1: WATER  WOOD  WOOD  WATER
    // Row 2: WATER  WATER WATER WATER

    // Fill with water.
    for (uint32_t x = 0; x < 4; ++x) {
        world->getData().at(x, 1).replaceMaterial(MaterialType::WATER, 1.0);
        world->getData().at(x, 2).replaceMaterial(MaterialType::WATER, 1.0);
    }

    // Add wood structure (same organism).
    world->getData().at(1, 1).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(1, 1).organism_id = 1;
    world->getData().at(2, 1).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 1).organism_id = 1;

    // Run several physics frames.
    for (int frame = 0; frame < 10; ++frame) {
        world->advanceTime(0.016);
    }

    // Verify wood cells have same velocity.
    const Cell& wood1 = world->getData().at(1, 1);
    const Cell& wood2 = world->getData().at(2, 1);

    EXPECT_NEAR(wood1.velocity.x, wood2.velocity.x, 0.001)
        << "Wood cells have different X velocities";
    EXPECT_NEAR(wood1.velocity.y, wood2.velocity.y, 0.001)
        << "Wood cells have different Y velocities";
}

TEST_F(RigidBodyIntegrationTest, MultipleStructuresMoveIndependently)
{
    auto world = createWorld(10, 10);

    // Create two separate structures with different forces.
    // Structure 1: 2 cells at y=3.
    world->getData().at(2, 3).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 3).organism_id = 1;
    world->getData().at(3, 3).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(3, 3).organism_id = 1;

    // Structure 2: 2 cells at y=6.
    world->getData().at(6, 6).replaceMaterial(MaterialType::METAL, 1.0);
    world->getData().at(6, 6).organism_id = 2;
    world->getData().at(7, 6).replaceMaterial(MaterialType::METAL, 1.0);
    world->getData().at(7, 6).organism_id = 2;

    // Run physics.
    for (int frame = 0; frame < 10; ++frame) {
        world->advanceTime(0.016);
    }

    // Each structure should have unified velocity within itself.
    const Cell& wood1 = world->getData().at(2, 3);
    const Cell& wood2 = world->getData().at(3, 3);
    EXPECT_NEAR(wood1.velocity.x, wood2.velocity.x, 0.0001);
    EXPECT_NEAR(wood1.velocity.y, wood2.velocity.y, 0.0001);

    const Cell& metal1 = world->getData().at(6, 6);
    const Cell& metal2 = world->getData().at(7, 6);
    EXPECT_NEAR(metal1.velocity.x, metal2.velocity.x, 0.0001);
    EXPECT_NEAR(metal1.velocity.y, metal2.velocity.y, 0.0001);

    // But structures should have DIFFERENT velocities (different mass, falling from different
    // heights). They won't necessarily be different in all cases, but they should at least both be
    // falling.
    EXPECT_GT(wood1.velocity.y, 0.1);
    EXPECT_GT(metal1.velocity.y, 0.1);
}
