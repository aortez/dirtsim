#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldRigidBodyCalculator.h"
#include <gtest/gtest.h>

using namespace DirtSim;

class RigidBodyCalculatorTest : public ::testing::Test {
protected:
    WorldRigidBodyCalculator calculator;

    std::unique_ptr<World> createWorld(uint32_t width, uint32_t height)
    {
        auto world = std::make_unique<World>(width, height);
        // Clear to air.
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                world->getData().at(x, y).replaceMaterial(MaterialType::AIR, 0.0);
            }
        }
        return world;
    }
};

TEST_F(RigidBodyCalculatorTest, SingleWoodCellFormsStructure)
{
    auto world = createWorld(5, 5);
    world->getData().at(2, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 2).organism_id = 1;

    auto structure = calculator.findConnectedStructure(*world, { 2, 2 });

    EXPECT_EQ(structure.size(), 1u);
    EXPECT_EQ(structure.cells[0], (Vector2i{ 2, 2 }));
    EXPECT_EQ(structure.organism_id, 1u);
}

TEST_F(RigidBodyCalculatorTest, NonOrganismCellReturnsEmpty)
{
    auto world = createWorld(5, 5);
    // Cell without organism_id should not form structure.
    world->getData().at(2, 2).replaceMaterial(MaterialType::WOOD, 1.0);

    auto structure = calculator.findConnectedStructure(*world, { 2, 2 });

    EXPECT_TRUE(structure.empty());
}

TEST_F(RigidBodyCalculatorTest, LShapedWoodConnects)
{
    auto world = createWorld(5, 5);

    // L-shape (all same organism):
    //   W
    //   W
    //   W W W
    world->getData().at(1, 0).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(1, 0).organism_id = 1;
    world->getData().at(1, 1).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(1, 1).organism_id = 1;
    world->getData().at(1, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(1, 2).organism_id = 1;
    world->getData().at(2, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 2).organism_id = 1;
    world->getData().at(3, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(3, 2).organism_id = 1;

    auto structure = calculator.findConnectedStructure(*world, { 1, 0 });

    EXPECT_EQ(structure.size(), 5u);
}

TEST_F(RigidBodyCalculatorTest, DiagonalDoesNotConnect)
{
    auto world = createWorld(5, 5);

    // Diagonal (should NOT connect):
    //   W .
    //   . W
    world->getData().at(1, 1).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(1, 1).organism_id = 1;
    world->getData().at(2, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 2).organism_id = 1;

    auto structure = calculator.findConnectedStructure(*world, { 1, 1 });

    EXPECT_EQ(structure.size(), 1u);
}

TEST_F(RigidBodyCalculatorTest, DifferentOrganismIdDoesNotConnect)
{
    auto world = createWorld(5, 5);

    // Two adjacent wood cells with different organism IDs.
    world->getData().at(2, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 2).organism_id = 1;

    world->getData().at(3, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(3, 2).organism_id = 2;

    auto structure = calculator.findConnectedStructure(*world, { 2, 2 }, 1);

    EXPECT_EQ(structure.size(), 1u);
    EXPECT_EQ(structure.organism_id, 1u);
}

TEST_F(RigidBodyCalculatorTest, SameOrganismIdConnects)
{
    auto world = createWorld(5, 5);

    // Two adjacent wood cells with same organism ID.
    world->getData().at(2, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 2).organism_id = 42;

    world->getData().at(3, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(3, 2).organism_id = 42;

    auto structure = calculator.findConnectedStructure(*world, { 2, 2 }, 42);

    EXPECT_EQ(structure.size(), 2u);
    EXPECT_EQ(structure.organism_id, 42u);
}

TEST_F(RigidBodyCalculatorTest, FindAllStructuresFindsMultiple)
{
    auto world = createWorld(10, 5);

    // Two separate structures.
    // Structure 1: cells at (1,2), (2,2).
    world->getData().at(1, 2).replaceMaterial(MaterialType::METAL, 1.0);
    world->getData().at(1, 2).organism_id = 1;
    world->getData().at(2, 2).replaceMaterial(MaterialType::METAL, 1.0);
    world->getData().at(2, 2).organism_id = 1;

    // Structure 2: cells at (7,2), (8,2).
    world->getData().at(7, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(7, 2).organism_id = 2;
    world->getData().at(8, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(8, 2).organism_id = 2;

    auto structures = calculator.findAllStructures(*world);

    EXPECT_EQ(structures.size(), 2u);
}

TEST_F(RigidBodyCalculatorTest, CalculateMassIsSumOfCellMasses)
{
    auto world = createWorld(5, 5);

    // Two wood cells, full fill.
    world->getData().at(2, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 2).organism_id = 1;
    world->getData().at(3, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(3, 2).organism_id = 1;

    auto structure = calculator.findConnectedStructure(*world, { 2, 2 });
    double mass = calculator.calculateStructureMass(*world, structure);

    double expected = 2.0 * getMaterialProperties(MaterialType::WOOD).density;
    EXPECT_DOUBLE_EQ(mass, expected);
}

TEST_F(RigidBodyCalculatorTest, CalculateCOMIsWeightedCenter)
{
    auto world = createWorld(5, 5);

    // Two equal cells at x=2 and x=3, COM should be at x=2.5.
    world->getData().at(2, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 2).organism_id = 1;
    world->getData().at(3, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(3, 2).organism_id = 1;

    auto structure = calculator.findConnectedStructure(*world, { 2, 2 });
    Vector2d com = calculator.calculateStructureCOM(*world, structure);

    EXPECT_NEAR(com.x, 2.5, 0.01);
    EXPECT_NEAR(com.y, 2.0, 0.01);
}

TEST_F(RigidBodyCalculatorTest, GatherForcesIsSumOfPendingForces)
{
    auto world = createWorld(5, 5);

    world->getData().at(2, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 2).organism_id = 1;
    world->getData().at(2, 2).pending_force = { 1.0, 2.0 };

    world->getData().at(3, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(3, 2).organism_id = 1;
    world->getData().at(3, 2).pending_force = { 0.5, -1.0 };

    auto structure = calculator.findConnectedStructure(*world, { 2, 2 });
    Vector2d force = calculator.gatherStructureForces(*world, structure);

    EXPECT_DOUBLE_EQ(force.x, 1.5);
    EXPECT_DOUBLE_EQ(force.y, 1.0);
}

TEST_F(RigidBodyCalculatorTest, ApplyUnifiedVelocitySetsAllCellsToSameVelocity)
{
    auto world = createWorld(5, 5);

    // Create 3-cell structure with different pending forces.
    world->getData().at(1, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(1, 2).organism_id = 1;
    world->getData().at(1, 2).pending_force = { 1.0, -2.0 };

    world->getData().at(2, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(2, 2).organism_id = 1;
    world->getData().at(2, 2).pending_force = { 0.0, -1.0 };

    world->getData().at(3, 2).replaceMaterial(MaterialType::WOOD, 1.0);
    world->getData().at(3, 2).organism_id = 1;
    world->getData().at(3, 2).pending_force = { -1.0, -1.0 };

    auto structure = calculator.findConnectedStructure(*world, { 1, 2 });
    double dt = 0.016;
    calculator.applyUnifiedVelocity(*world, structure, dt);

    // All cells should have identical velocity.
    const Cell& cell1 = world->getData().at(1, 2);
    const Cell& cell2 = world->getData().at(2, 2);
    const Cell& cell3 = world->getData().at(3, 2);

    EXPECT_DOUBLE_EQ(cell1.velocity.x, cell2.velocity.x);
    EXPECT_DOUBLE_EQ(cell1.velocity.x, cell3.velocity.x);
    EXPECT_DOUBLE_EQ(cell1.velocity.y, cell2.velocity.y);
    EXPECT_DOUBLE_EQ(cell1.velocity.y, cell3.velocity.y);

    // Velocity should be based on total force / total mass.
    // Total force: (1-1, -2-1-1) = (0, -4).
    // Total mass: 3 * wood_density.
    double wood_density = getMaterialProperties(MaterialType::WOOD).density;
    double total_mass = 3.0 * wood_density;
    double expected_vy = (-4.0 / total_mass) * dt;

    EXPECT_NEAR(cell1.velocity.x, 0.0, 0.001);
    EXPECT_NEAR(cell1.velocity.y, expected_vy, 0.001);
}

TEST_F(RigidBodyCalculatorTest, ApplyUnifiedVelocityUpdatesStructureVelocity)
{
    auto world = createWorld(5, 5);

    world->getData().at(2, 2).replaceMaterial(MaterialType::METAL, 1.0);
    world->getData().at(2, 2).organism_id = 1;
    world->getData().at(2, 2).pending_force = { 10.0, -5.0 };

    auto structure = calculator.findConnectedStructure(*world, { 2, 2 });
    EXPECT_DOUBLE_EQ(structure.velocity.x, 0.0); // Initial velocity.
    EXPECT_DOUBLE_EQ(structure.velocity.y, 0.0);

    double dt = 0.016;
    calculator.applyUnifiedVelocity(*world, structure, dt);

    // Structure velocity should be updated.
    double metal_density = getMaterialProperties(MaterialType::METAL).density;
    double expected_vx = (10.0 / metal_density) * dt;
    double expected_vy = (-5.0 / metal_density) * dt;

    EXPECT_NEAR(structure.velocity.x, expected_vx, 0.001);
    EXPECT_NEAR(structure.velocity.y, expected_vy, 0.001);
}
