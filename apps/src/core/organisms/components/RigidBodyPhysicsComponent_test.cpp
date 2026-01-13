/**
 * Tests for RigidBodyPhysicsComponent.
 *
 * Verifies force gathering, air resistance, and F=ma integration for
 * multi-cell rigid body organisms.
 */

#include "RigidBodyPhysicsComponent.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <gtest/gtest.h>

using namespace DirtSim;

class RigidBodyPhysicsComponentTest : public ::testing::Test {
protected:
    std::unique_ptr<World> createWorld(uint32_t width = 10, uint32_t height = 10)
    {
        return std::make_unique<World>(width, height);
    }
};

// =============================================================================
// Force Gathering
// =============================================================================

TEST_F(RigidBodyPhysicsComponentTest, GatherForcesSumsFromCells)
{
    auto world = createWorld();
    RigidBodyPhysicsComponent physics;

    // Set pending forces on two cells.
    world->getData().at(5, 5).pending_force = { 10.0, 5.0 };
    world->getData().at(5, 6).pending_force = { 3.0, -2.0 };

    std::vector<Vector2i> cells = { { 5, 5 }, { 5, 6 } };
    physics.gatherForces(*world, cells);

    const auto force = physics.getPendingForce();
    EXPECT_NEAR(force.x, 13.0, 0.0001);
    EXPECT_NEAR(force.y, 3.0, 0.0001);
}

TEST_F(RigidBodyPhysicsComponentTest, EmptyCellsContributeZeroForce)
{
    auto world = createWorld();
    RigidBodyPhysicsComponent physics;

    // Cell at (5,5) is AIR with no pending force.
    ASSERT_EQ(world->getData().at(5, 5).material_type, Material::EnumType::Air);

    std::vector<Vector2i> cells = { { 5, 5 } };
    physics.gatherForces(*world, cells);

    const auto force = physics.getPendingForce();
    EXPECT_NEAR(force.x, 0.0, 0.0001);
    EXPECT_NEAR(force.y, 0.0, 0.0001);
}

// =============================================================================
// Force Accumulation
// =============================================================================

TEST_F(RigidBodyPhysicsComponentTest, AddForceAccumulates)
{
    RigidBodyPhysicsComponent physics;

    physics.addForce({ 5.0, 3.0 });
    physics.addForce({ 2.0, -1.0 });

    const auto force = physics.getPendingForce();
    EXPECT_NEAR(force.x, 7.0, 0.0001);
    EXPECT_NEAR(force.y, 2.0, 0.0001);
}

TEST_F(RigidBodyPhysicsComponentTest, ClearPendingForceResetsToZero)
{
    RigidBodyPhysicsComponent physics;

    physics.addForce({ 10.0, 20.0 });
    physics.clearPendingForce();

    const auto force = physics.getPendingForce();
    EXPECT_NEAR(force.x, 0.0, 0.0001);
    EXPECT_NEAR(force.y, 0.0, 0.0001);
}

// =============================================================================
// Integration (F=ma)
// =============================================================================

TEST_F(RigidBodyPhysicsComponentTest, IntegrateFollowsFEqualsMA)
{
    RigidBodyPhysicsComponent physics;

    physics.addForce({ 10.0, -5.0 });

    Vector2d velocity{ 0.0, 0.0 };
    const double mass = 2.0;
    const double dt = 0.1;

    physics.integrate(velocity, mass, dt);

    // a = F/m, v += a * dt.
    // ax = 10/2 = 5, ay = -5/2 = -2.5
    // vx = 5 * 0.1 = 0.5, vy = -2.5 * 0.1 = -0.25
    EXPECT_NEAR(velocity.x, 0.5, 0.0001);
    EXPECT_NEAR(velocity.y, -0.25, 0.0001);
}

TEST_F(RigidBodyPhysicsComponentTest, IntegrateAccumulatesVelocity)
{
    RigidBodyPhysicsComponent physics;

    Vector2d velocity{ 1.0, 0.0 };
    const double mass = 1.0;
    const double dt = 0.1;

    // Apply force over multiple frames.
    for (int i = 0; i < 10; ++i) {
        physics.clearPendingForce();
        physics.addForce({ 0.0, 10.0 });
        physics.integrate(velocity, mass, dt);
    }

    EXPECT_NEAR(velocity.x, 1.0, 0.0001);  // X unchanged.
    EXPECT_NEAR(velocity.y, 10.0, 0.0001); // Y accumulated.
}

TEST_F(RigidBodyPhysicsComponentTest, IntegrateWithZeroMassDoesNothing)
{
    RigidBodyPhysicsComponent physics;

    physics.addForce({ 100.0, 100.0 });

    Vector2d velocity{ 5.0, 5.0 };
    physics.integrate(velocity, 0.0, 0.1);

    // Velocity unchanged due to zero mass guard.
    EXPECT_NEAR(velocity.x, 5.0, 0.0001);
    EXPECT_NEAR(velocity.y, 5.0, 0.0001);
}

// =============================================================================
// Air Resistance
// =============================================================================

TEST_F(RigidBodyPhysicsComponentTest, AirResistanceOpposesMotion)
{
    auto world = createWorld();
    RigidBodyPhysicsComponent physics(Material::EnumType::Wood);

    const Vector2d velocity{ 10.0, 0.0 };
    physics.applyAirResistance(*world, velocity);

    const auto force = physics.getPendingForce();

    // Force should oppose motion (negative x).
    EXPECT_LT(force.x, 0.0);
    EXPECT_NEAR(force.y, 0.0, 0.0001);
}

TEST_F(RigidBodyPhysicsComponentTest, AirResistanceZeroWhenStationary)
{
    auto world = createWorld();
    RigidBodyPhysicsComponent physics(Material::EnumType::Wood);

    const Vector2d velocity{ 0.0, 0.0 };
    physics.applyAirResistance(*world, velocity);

    const auto force = physics.getPendingForce();
    EXPECT_NEAR(force.x, 0.0, 0.0001);
    EXPECT_NEAR(force.y, 0.0, 0.0001);
}

TEST_F(RigidBodyPhysicsComponentTest, AirResistanceScalesWithVelocitySquared)
{
    auto world = createWorld();

    RigidBodyPhysicsComponent physics1(Material::EnumType::Wood);
    RigidBodyPhysicsComponent physics2(Material::EnumType::Wood);

    physics1.applyAirResistance(*world, { 5.0, 0.0 });
    physics2.applyAirResistance(*world, { 10.0, 0.0 });

    const double force1 = std::abs(physics1.getPendingForce().x);
    const double force2 = std::abs(physics2.getPendingForce().x);

    // Double velocity should give ~4x drag (v^2 relationship).
    EXPECT_NEAR(force2 / force1, 4.0, 0.1);
}
