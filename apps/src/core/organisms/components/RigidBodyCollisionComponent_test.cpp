/**
 * Tests for RigidBodyCollisionComponent.
 *
 * Verifies collision detection and response for multi-cell rigid body organisms.
 */

#include "RigidBodyCollisionComponent.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {
const OrganismId kOrganism1{ 1 };
} // namespace

class RigidBodyCollisionComponentTest : public ::testing::Test {
protected:
    std::unique_ptr<World> createWorld(uint32_t width = 10, uint32_t height = 10)
    {
        return std::make_unique<World>(width, height);
    }
};

// =============================================================================
// Detection - World Boundaries
// =============================================================================

TEST_F(RigidBodyCollisionComponentTest, DetectsLeftBoundaryCollision)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    std::vector<Vector2i> currentCells = { { 1, 5 } };
    std::vector<Vector2i> predictedCells = { { -1, 5 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_TRUE(result.blocked);
    EXPECT_EQ(result.blockedCells.size(), 1);
    EXPECT_GT(result.contactNormal.x, 0.0); // Normal points right (inward).
}

TEST_F(RigidBodyCollisionComponentTest, DetectsRightBoundaryCollision)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    std::vector<Vector2i> currentCells = { { 8, 5 } };
    std::vector<Vector2i> predictedCells = { { 10, 5 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_TRUE(result.blocked);
    EXPECT_LT(result.contactNormal.x, 0.0); // Normal points left (inward).
}

TEST_F(RigidBodyCollisionComponentTest, DetectsBottomBoundaryCollision)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    std::vector<Vector2i> currentCells = { { 5, 8 } };
    std::vector<Vector2i> predictedCells = { { 5, 10 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_TRUE(result.blocked);
    EXPECT_LT(result.contactNormal.y, 0.0); // Normal points up (inward).
}

// =============================================================================
// Detection - WALL Material
// =============================================================================

TEST_F(RigidBodyCollisionComponentTest, DetectsWallCollision)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Place a WALL cell.
    world->getData().at(5, 7).material_type = Material::EnumType::Wall;
    world->getData().at(5, 7).fill_ratio = 1.0;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    std::vector<Vector2i> predictedCells = { { 5, 7 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_TRUE(result.blocked);
    EXPECT_EQ(result.blockedCells.size(), 1);
    EXPECT_EQ(result.blockedCells[0], Vector2i(5, 7));
}

TEST_F(RigidBodyCollisionComponentTest, WallCollisionNormalPointsAwayFromWall)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Wall below organism.
    world->getData().at(5, 7).material_type = Material::EnumType::Wall;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    std::vector<Vector2i> predictedCells = { { 5, 7 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_TRUE(result.blocked);
    // Normal should point up (away from wall below).
    EXPECT_LT(result.contactNormal.y, 0.0);
}

// =============================================================================
// Detection - Other Organisms
// =============================================================================

TEST_F(RigidBodyCollisionComponentTest, DetectsOtherOrganismCollision)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Create another organism at the target position.
    OrganismId otherOrganism = world->getOrganismManager().createTree(*world, 5, 7);

    // Use a different organism ID for the "moving" organism.
    OrganismId movingOrganism{ 999 };

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    std::vector<Vector2i> predictedCells = { { 5, 7 } };

    auto result = collision.detect(*world, movingOrganism, currentCells, predictedCells);

    EXPECT_TRUE(result.blocked);
    (void)otherOrganism; // Silence unused variable warning.
}

TEST_F(RigidBodyCollisionComponentTest, NoCollisionWithOwnCells)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Create organism at (5, 5) and add another cell at (5, 6).
    OrganismId myOrganism = world->getOrganismManager().createTree(*world, 5, 5);
    world->getData().at(5, 6).material_type = Material::EnumType::Wood;
    world->getData().at(5, 6).fill_ratio = 1.0;
    world->getOrganismManager().addCellToOrganism(myOrganism, { 5, 6 });

    std::vector<Vector2i> currentCells = { { 5, 5 }, { 5, 6 } };
    std::vector<Vector2i> predictedCells = { { 5, 6 }, { 5, 7 } };

    auto result = collision.detect(*world, myOrganism, currentCells, predictedCells);

    // Should not be blocked by own cells.
    EXPECT_FALSE(result.blocked);
}

// =============================================================================
// Detection - Dense Solids
// =============================================================================

TEST_F(RigidBodyCollisionComponentTest, DetectsDenseSolidCollision)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Place dense dirt.
    world->getData().at(5, 7).material_type = Material::EnumType::Dirt;
    world->getData().at(5, 7).fill_ratio = 0.9;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    std::vector<Vector2i> predictedCells = { { 5, 7 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_TRUE(result.blocked);
}

TEST_F(RigidBodyCollisionComponentTest, NoCollisionWithSparseSolid)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Sparse dirt (below threshold).
    world->getData().at(5, 7).material_type = Material::EnumType::Dirt;
    world->getData().at(5, 7).fill_ratio = 0.5;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    std::vector<Vector2i> predictedCells = { { 5, 7 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_FALSE(result.blocked);
}

// =============================================================================
// Detection - Empty Space
// =============================================================================

TEST_F(RigidBodyCollisionComponentTest, NoCollisionWithEmptySpace)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    std::vector<Vector2i> predictedCells = { { 5, 6 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_FALSE(result.blocked);
}

TEST_F(RigidBodyCollisionComponentTest, NoCollisionWithWater)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Water is not solid.
    world->getData().at(5, 7).material_type = Material::EnumType::Water;
    world->getData().at(5, 7).fill_ratio = 1.0;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    std::vector<Vector2i> predictedCells = { { 5, 7 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_FALSE(result.blocked);
}

// =============================================================================
// Response - Velocity Modification
// =============================================================================

TEST_F(RigidBodyCollisionComponentTest, ResponseZerosVelocityIntoSurface)
{
    RigidBodyCollisionComponent collision;

    CollisionResult result;
    result.blocked = true;
    result.contactNormal = { 0.0, -1.0 }; // Floor normal (points up).

    Vector2d velocity{ 0.0, 5.0 }; // Moving down into floor.
    collision.respond(result, velocity, 0.0);

    EXPECT_NEAR(velocity.y, 0.0, 0.0001);
}

TEST_F(RigidBodyCollisionComponentTest, ResponsePreservesTangentialVelocity)
{
    RigidBodyCollisionComponent collision;

    CollisionResult result;
    result.blocked = true;
    result.contactNormal = { 0.0, -1.0 }; // Floor normal.

    Vector2d velocity{ 3.0, 5.0 }; // Moving diagonally into floor.
    collision.respond(result, velocity, 0.0);

    EXPECT_NEAR(velocity.x, 3.0, 0.0001); // Horizontal preserved.
    EXPECT_NEAR(velocity.y, 0.0, 0.0001); // Vertical zeroed.
}

TEST_F(RigidBodyCollisionComponentTest, ResponseWithRestitutionBounces)
{
    RigidBodyCollisionComponent collision;

    CollisionResult result;
    result.blocked = true;
    result.contactNormal = { 0.0, -1.0 }; // Floor normal.

    Vector2d velocity{ 0.0, 5.0 };            // Moving down.
    collision.respond(result, velocity, 1.0); // Full restitution.

    EXPECT_NEAR(velocity.y, -5.0, 0.0001); // Full bounce.
}

TEST_F(RigidBodyCollisionComponentTest, ResponseWithPartialRestitution)
{
    RigidBodyCollisionComponent collision;

    CollisionResult result;
    result.blocked = true;
    result.contactNormal = { 0.0, -1.0 };

    Vector2d velocity{ 0.0, 10.0 };
    collision.respond(result, velocity, 0.5);

    EXPECT_NEAR(velocity.y, -5.0, 0.0001); // Half bounce.
}

TEST_F(RigidBodyCollisionComponentTest, ResponseIgnoresVelocityAwayFromSurface)
{
    RigidBodyCollisionComponent collision;

    CollisionResult result;
    result.blocked = true;
    result.contactNormal = { 0.0, -1.0 }; // Floor normal.

    Vector2d velocity{ 0.0, -5.0 }; // Moving up (away from floor).
    collision.respond(result, velocity, 0.0);

    EXPECT_NEAR(velocity.y, -5.0, 0.0001); // Unchanged.
}

TEST_F(RigidBodyCollisionComponentTest, ResponseNoOpWhenNotBlocked)
{
    RigidBodyCollisionComponent collision;

    CollisionResult result;
    result.blocked = false;

    Vector2d velocity{ 5.0, 5.0 };
    collision.respond(result, velocity, 0.0);

    EXPECT_NEAR(velocity.x, 5.0, 0.0001);
    EXPECT_NEAR(velocity.y, 5.0, 0.0001);
}

// =============================================================================
// Multi-Cell Scenarios
// =============================================================================

TEST_F(RigidBodyCollisionComponentTest, DetectsPartialBlockingMultiCell)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Wall on one side only.
    world->getData().at(6, 5).material_type = Material::EnumType::Wall;

    std::vector<Vector2i> currentCells = { { 4, 5 }, { 5, 5 } };
    std::vector<Vector2i> predictedCells = { { 5, 5 }, { 6, 5 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_TRUE(result.blocked);
    EXPECT_EQ(result.blockedCells.size(), 1);
}

TEST_F(RigidBodyCollisionComponentTest, ContactNormalFromMultipleBlockedCells)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Corner collision - walls on right and below.
    world->getData().at(7, 5).material_type = Material::EnumType::Wall;
    world->getData().at(6, 6).material_type = Material::EnumType::Wall;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    std::vector<Vector2i> predictedCells = { { 7, 5 }, { 6, 6 } };

    auto result = collision.detect(*world, kOrganism1, currentCells, predictedCells);

    EXPECT_TRUE(result.blocked);
    EXPECT_EQ(result.blockedCells.size(), 2);

    // Normal should be normalized and point away from both obstacles.
    double len = std::sqrt(
        result.contactNormal.x * result.contactNormal.x
        + result.contactNormal.y * result.contactNormal.y);
    EXPECT_NEAR(len, 1.0, 0.01);
}

// =============================================================================
// Ground Friction
// =============================================================================

TEST_F(RigidBodyCollisionComponentTest, ComputeGroundFrictionReturnsZeroWhenNotOnGround)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    Vector2d velocity{ 10.0, 0.0 }; // Moving horizontally.
    double normalForce = 0.0;       // No ground contact.

    Vector2d friction =
        collision.computeGroundFriction(*world, kOrganism1, currentCells, velocity, normalForce);

    EXPECT_NEAR(friction.x, 0.0, 0.0001);
    EXPECT_NEAR(friction.y, 0.0, 0.0001);
}

TEST_F(RigidBodyCollisionComponentTest, ComputeGroundFrictionOpposesHorizontalVelocity)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Place DIRT ground below organism.
    world->getData().at(5, 6).material_type = Material::EnumType::Dirt;
    world->getData().at(5, 6).fill_ratio = 1.0;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    Vector2d velocity{ 10.0, 0.0 }; // Moving right.
    double normalForce = 100.0;     // On ground.

    Vector2d friction =
        collision.computeGroundFriction(*world, kOrganism1, currentCells, velocity, normalForce);

    // Friction should oppose motion (point left).
    EXPECT_LT(friction.x, 0.0);
    EXPECT_NEAR(friction.y, 0.0, 0.0001); // No vertical friction.
}

TEST_F(RigidBodyCollisionComponentTest, ComputeGroundFrictionProportionalToNormalForce)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Place DIRT ground below organism.
    world->getData().at(5, 6).material_type = Material::EnumType::Dirt;
    world->getData().at(5, 6).fill_ratio = 1.0;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    Vector2d velocity{ 10.0, 0.0 };

    // Test with two different normal forces.
    double normalForce1 = 50.0;
    double normalForce2 = 100.0;

    Vector2d friction1 =
        collision.computeGroundFriction(*world, kOrganism1, currentCells, velocity, normalForce1);
    Vector2d friction2 =
        collision.computeGroundFriction(*world, kOrganism1, currentCells, velocity, normalForce2);

    // Friction should be proportional to normal force.
    // friction2 should be approximately 2x friction1.
    double ratio = std::abs(friction2.x / friction1.x);
    EXPECT_NEAR(ratio, 2.0, 0.1);
}

TEST_F(RigidBodyCollisionComponentTest, ComputeGroundFrictionUsesStaticCoefficientWhenSlow)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Place DIRT ground (static_friction=1.5, stick_velocity=0.1).
    world->getData().at(5, 6).material_type = Material::EnumType::Dirt;
    world->getData().at(5, 6).fill_ratio = 1.0;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    Vector2d velocity{ 0.05, 0.0 }; // Below stick_velocity.
    double normalForce = 100.0;

    Vector2d friction =
        collision.computeGroundFriction(*world, kOrganism1, currentCells, velocity, normalForce);

    // Friction magnitude should be close to static_friction * normal_force.
    // DIRT static_friction ≈ 1.5, so expect |friction| ≈ 150.
    double frictionMag = std::abs(friction.x);
    EXPECT_GT(frictionMag, 100.0); // Higher than kinetic.
}

TEST_F(RigidBodyCollisionComponentTest, ComputeGroundFrictionUsesKineticCoefficientWhenFast)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Place DIRT ground (kinetic_friction=0.5, stick_velocity=0.1, transition_width=0.1).
    world->getData().at(5, 6).material_type = Material::EnumType::Dirt;
    world->getData().at(5, 6).fill_ratio = 1.0;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    Vector2d velocity{ 10.0, 0.0 }; // Well above stick_velocity + transition_width.
    double normalForce = 100.0;

    Vector2d friction =
        collision.computeGroundFriction(*world, kOrganism1, currentCells, velocity, normalForce);

    // Friction magnitude should be close to kinetic_friction * normal_force.
    // DIRT kinetic_friction = 0.5, so expect |friction| ≈ 50.
    double frictionMag = std::abs(friction.x);
    EXPECT_LT(frictionMag, 100.0); // Lower than static.
    EXPECT_GT(frictionMag, 30.0);  // Reasonable kinetic friction.
}

TEST_F(RigidBodyCollisionComponentTest, ComputeGroundFrictionZeroWhenStationary)
{
    auto world = createWorld();
    RigidBodyCollisionComponent collision;

    // Place DIRT ground below organism.
    world->getData().at(5, 6).material_type = Material::EnumType::Dirt;
    world->getData().at(5, 6).fill_ratio = 1.0;

    std::vector<Vector2i> currentCells = { { 5, 5 } };
    Vector2d velocity{ 0.0, 0.0 }; // Stationary.
    double normalForce = 100.0;

    Vector2d friction =
        collision.computeGroundFriction(*world, kOrganism1, currentCells, velocity, normalForce);

    // No velocity = no friction force.
    EXPECT_NEAR(friction.x, 0.0, 0.0001);
    EXPECT_NEAR(friction.y, 0.0, 0.0001);
}
