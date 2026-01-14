/**
 * Tests for organism rigid body physics.
 *
 * These tests verify the core physics behavior of organisms as continuous-space
 * rigid bodies: position/velocity integration, mass computation, and gravity.
 */

#include "core/MaterialType.h"
#include "core/Vector2d.h"
#include "core/Vector2i.h"
#include "core/organisms/Body.h"
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

// Concrete organism for testing (Organism::Body is abstract).
class TestOrganism : public Organism::Body {
public:
    TestOrganism(OrganismId id, OrganismType type) : Organism::Body(id, type) {}

    Vector2i getAnchorCell() const override
    {
        return Vector2i{ static_cast<int>(position.x), static_cast<int>(position.y) };
    }

    void setAnchorCell(Vector2i pos) override
    {
        position = { static_cast<double>(pos.x), static_cast<double>(pos.y) };
    }

    void update(World& /*world*/, double /*deltaTime*/) override
    {
        // No-op for physics tests.
    }
};

} // namespace

class OrganismPhysicsTest : public ::testing::Test {
protected:
    // Helper to create a simple single-cell organism.
    std::unique_ptr<TestOrganism> createSingleCellOrganism(
        Vector2d pos, Material::EnumType material)
    {
        auto org = std::make_unique<TestOrganism>(OrganismId{ 1 }, OrganismType::TREE);
        org->position = pos;
        org->velocity = { 0.0, 0.0 };

        org->local_shape.push_back({
            .localPos = { 0, 0 },
            .material = material,
            .fillRatio = 1.0,
        });

        org->recomputeMass();
        org->recomputeCenterOfMass();

        return org;
    }

    // Helper to create a multi-cell organism (horizontal 3-cell beam).
    std::unique_ptr<TestOrganism> createHorizontalBeam(Vector2d pos)
    {
        auto org = std::make_unique<TestOrganism>(OrganismId{ 2 }, OrganismType::TREE);
        org->position = pos;
        org->velocity = { 0.0, 0.0 };

        // Three WOOD cells in a row: local positions (0,0), (1,0), (2,0).
        org->local_shape.push_back(
            { .localPos = { 0, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
        org->local_shape.push_back(
            { .localPos = { 1, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
        org->local_shape.push_back(
            { .localPos = { 2, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });

        org->recomputeMass();
        org->recomputeCenterOfMass();

        return org;
    }
};

// =============================================================================
// Position and Velocity Integration
// =============================================================================

TEST_F(OrganismPhysicsTest, PositionUpdatesWithVelocity)
{
    auto org = createSingleCellOrganism({ 5.0, 5.0 }, Material::EnumType::Wood);
    org->velocity = { 1.0, 0.5 };

    double dt = 0.1;
    org->integratePosition(dt);

    EXPECT_NEAR(org->position.x, 5.1, 0.0001);
    EXPECT_NEAR(org->position.y, 5.05, 0.0001);
}

TEST_F(OrganismPhysicsTest, PositionAccumulatesOverMultipleFrames)
{
    auto org = createSingleCellOrganism({ 0.0, 0.0 }, Material::EnumType::Wood);
    org->velocity = { 0.5, 0.5 };

    double dt = 0.016;
    for (int i = 0; i < 100; ++i) {
        org->integratePosition(dt);
    }

    // After 100 frames at dt=0.016, total time = 1.6s.
    // position = velocity * time = (0.5, 0.5) * 1.6 = (0.8, 0.8).
    EXPECT_NEAR(org->position.x, 0.8, 0.001);
    EXPECT_NEAR(org->position.y, 0.8, 0.001);
}

TEST_F(OrganismPhysicsTest, VelocityUpdatesWithForce)
{
    auto org = createSingleCellOrganism({ 5.0, 5.0 }, Material::EnumType::Wood);
    org->velocity = { 0.0, 0.0 };

    // Apply a force.
    Vector2d force = { 10.0, -5.0 };
    double dt = 0.1;
    org->applyForce(force, dt);

    // a = F/m, v += a * dt.
    double expected_ax = force.x / org->mass;
    double expected_ay = force.y / org->mass;

    EXPECT_NEAR(org->velocity.x, expected_ax * dt, 0.0001);
    EXPECT_NEAR(org->velocity.y, expected_ay * dt, 0.0001);
}

TEST_F(OrganismPhysicsTest, VelocityAccumulatesForces)
{
    auto org = createSingleCellOrganism({ 5.0, 5.0 }, Material::EnumType::Wood);
    org->velocity = { 1.0, 0.0 }; // Initial velocity.

    Vector2d force = { 0.0, 10.0 }; // Downward force.
    double dt = 0.1;

    // Apply force over multiple frames.
    for (int i = 0; i < 10; ++i) {
        org->applyForce(force, dt);
    }

    // Velocity should have increased in Y direction.
    EXPECT_NEAR(org->velocity.x, 1.0, 0.0001); // X unchanged.
    EXPECT_GT(org->velocity.y, 0.0);           // Y increased.
}

// =============================================================================
// Mass Computation
// =============================================================================

TEST_F(OrganismPhysicsTest, MassComputedFromSingleCell)
{
    auto org = createSingleCellOrganism({ 0.0, 0.0 }, Material::EnumType::Wood);

    double expected_mass = Material::getProperties(Material::EnumType::Wood).density * 1.0;
    EXPECT_NEAR(org->mass, expected_mass, 0.0001);
}

TEST_F(OrganismPhysicsTest, MassComputedFromMultipleCells)
{
    auto org = createHorizontalBeam({ 0.0, 0.0 });

    double wood_density = Material::getProperties(Material::EnumType::Wood).density;
    double expected_mass = wood_density * 3.0; // 3 cells, full fill.

    EXPECT_NEAR(org->mass, expected_mass, 0.0001);
}

TEST_F(OrganismPhysicsTest, MassAccountsForFillRatio)
{
    auto org = std::make_unique<TestOrganism>(OrganismId{ 1 }, OrganismType::TREE);
    org->position = { 0.0, 0.0 };
    org->velocity = { 0.0, 0.0 };

    // One cell at 50% fill.
    org->local_shape.push_back({
        .localPos = { 0, 0 },
        .material = Material::EnumType::Wood,
        .fillRatio = 0.5,
    });

    org->recomputeMass();

    double expected_mass = Material::getProperties(Material::EnumType::Wood).density * 0.5;
    EXPECT_NEAR(org->mass, expected_mass, 0.0001);
}

TEST_F(OrganismPhysicsTest, MassAccountsForDifferentMaterials)
{
    auto org = std::make_unique<TestOrganism>(OrganismId{ 1 }, OrganismType::TREE);
    org->position = { 0.0, 0.0 };
    org->velocity = { 0.0, 0.0 };

    // Mix of WOOD and METAL.
    org->local_shape.push_back(
        { .localPos = { 0, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
    org->local_shape.push_back(
        { .localPos = { 1, 0 }, .material = Material::EnumType::Metal, .fillRatio = 1.0 });

    org->recomputeMass();

    double wood_density = Material::getProperties(Material::EnumType::Wood).density;
    double metal_density = Material::getProperties(Material::EnumType::Metal).density;
    double expected_mass = wood_density + metal_density;

    EXPECT_NEAR(org->mass, expected_mass, 0.0001);
}

// =============================================================================
// Center of Mass Computation
// =============================================================================

TEST_F(OrganismPhysicsTest, COMAtOriginForSingleCellAtOrigin)
{
    auto org = createSingleCellOrganism({ 5.0, 5.0 }, Material::EnumType::Wood);

    // Single cell at local (0,0) -> COM should be at (0,0) relative to position.
    EXPECT_NEAR(org->center_of_mass.x, 0.0, 0.0001);
    EXPECT_NEAR(org->center_of_mass.y, 0.0, 0.0001);
}

TEST_F(OrganismPhysicsTest, COMAtCenterOfSymmetricShape)
{
    auto org = createHorizontalBeam({ 0.0, 0.0 });

    // Three cells at (0,0), (1,0), (2,0) with equal mass.
    // COM should be at (1, 0) - the middle cell.
    EXPECT_NEAR(org->center_of_mass.x, 1.0, 0.0001);
    EXPECT_NEAR(org->center_of_mass.y, 0.0, 0.0001);
}

TEST_F(OrganismPhysicsTest, COMShiftsTowardHeavierMaterial)
{
    auto org = std::make_unique<TestOrganism>(OrganismId{ 1 }, OrganismType::TREE);
    org->position = { 0.0, 0.0 };
    org->velocity = { 0.0, 0.0 };

    // WOOD at (0,0), METAL at (2,0). METAL is denser, so COM shifts right.
    org->local_shape.push_back(
        { .localPos = { 0, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
    org->local_shape.push_back(
        { .localPos = { 2, 0 }, .material = Material::EnumType::Metal, .fillRatio = 1.0 });

    org->recomputeMass();
    org->recomputeCenterOfMass();

    // COM should be between 0 and 2, but closer to 2 (where METAL is).
    EXPECT_GT(org->center_of_mass.x, 1.0);
    EXPECT_LT(org->center_of_mass.x, 2.0);
    EXPECT_NEAR(org->center_of_mass.y, 0.0, 0.0001);
}

// =============================================================================
// Gravity
// =============================================================================

TEST_F(OrganismPhysicsTest, GravityAcceleratesDownward)
{
    auto org = createSingleCellOrganism({ 5.0, 5.0 }, Material::EnumType::Wood);
    org->velocity = { 0.0, 0.0 };

    double gravity = 9.8;
    double dt = 0.1;

    // Apply gravity as a force: F = m * g.
    Vector2d gravity_force = { 0.0, org->mass * gravity };
    org->applyForce(gravity_force, dt);

    // Velocity should increase downward (positive Y in our coordinate system).
    EXPECT_NEAR(org->velocity.x, 0.0, 0.0001);
    EXPECT_NEAR(org->velocity.y, gravity * dt, 0.0001);
}

TEST_F(OrganismPhysicsTest, HeavierOrganismSameAcceleration)
{
    // Create light organism (1 cell).
    auto light_org = createSingleCellOrganism({ 0.0, 0.0 }, Material::EnumType::Wood);

    // Create heavy organism (3 cells).
    auto heavy_org = createHorizontalBeam({ 0.0, 0.0 });

    double gravity = 9.8;
    double dt = 0.1;

    // Apply gravity to both.
    Vector2d light_gravity = { 0.0, light_org->mass * gravity };
    Vector2d heavy_gravity = { 0.0, heavy_org->mass * gravity };

    light_org->applyForce(light_gravity, dt);
    heavy_org->applyForce(heavy_gravity, dt);

    // Both should have same acceleration (g), so same velocity change.
    // a = F/m = (m*g)/m = g.
    EXPECT_NEAR(light_org->velocity.y, gravity * dt, 0.0001);
    EXPECT_NEAR(heavy_org->velocity.y, gravity * dt, 0.0001);
    EXPECT_NEAR(light_org->velocity.y, heavy_org->velocity.y, 0.0001);
}
