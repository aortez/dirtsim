#include "RigidBodyComponent.h"
#include "LocalShapeProjection.h"
#include "RigidBodyCollisionComponent.h"
#include "RigidBodyPhysicsComponent.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/organisms/Body.h"
#include <cmath>

namespace DirtSim {

RigidBodyComponent::RigidBodyComponent(Material::EnumType material)
    : physics_(std::make_unique<RigidBodyPhysicsComponent>(material)),
      collision_(std::make_unique<RigidBodyCollisionComponent>()),
      projection_(std::make_unique<LocalShapeProjection>())
{}

RigidBodyComponent::~RigidBodyComponent() = default;

RigidBodyComponent::RigidBodyComponent(RigidBodyComponent&&) noexcept = default;
RigidBodyComponent& RigidBodyComponent::operator=(RigidBodyComponent&&) noexcept = default;

void RigidBodyComponent::addCell(Vector2i localPos, Material::EnumType material, double fillRatio)
{
    projection_->addCell(localPos, material, fillRatio);
}

RigidBodyUpdateResult RigidBodyComponent::update(
    OrganismId id,
    Vector2d& position,
    Vector2d& velocity,
    double mass,
    const std::vector<LocalCell>& localShape,
    World& world,
    double deltaTime,
    Vector2d externalForce,
    double verticalMargin)
{
    // Compute current grid cells from position.
    std::vector<Vector2i> currentCells;
    for (const auto& local : localShape) {
        Vector2d worldPos{ position.x + static_cast<double>(local.localPos.x),
                           position.y + static_cast<double>(local.localPos.y) };
        currentCells.push_back(Vector2i{ static_cast<int>(std::floor(worldPos.x)),
                                         static_cast<int>(std::floor(worldPos.y)) });
    }

    // Compute support and friction.
    double gravity = world.getPhysicsSettings().gravity;
    double weight = mass * std::abs(gravity);
    Vector2d gravityDir{ 0.0, gravity >= 0 ? 1.0 : -1.0 };

    Vector2d supportForce =
        collision_->computeSupportForce(world, id, currentCells, weight, gravityDir);
    double supportMagnitude = std::abs(supportForce.x) + std::abs(supportForce.y);
    bool onGround = supportMagnitude > 0.01;

    Vector2d frictionForce =
        collision_->computeGroundFriction(world, id, currentCells, velocity, supportMagnitude);

    // Apply forces.
    Vector2d gravityForce{ 0.0, mass * gravity };
    physics_->addForce(gravityForce);
    physics_->addForce(supportForce);
    physics_->addForce(frictionForce);

    if (externalForce.x != 0.0 || externalForce.y != 0.0) {
        physics_->addForce(externalForce);
    }

    physics_->applyAirResistance(world, velocity);

    // Integrate.
    physics_->integrate(velocity, mass, deltaTime);
    physics_->clearPendingForce();

    // Predict position and check collisions.
    Vector2d desiredPosition{ position.x + velocity.x * deltaTime,
                              position.y + velocity.y * deltaTime };

    std::vector<Vector2i> predictedCells;
    for (const auto& local : localShape) {
        Vector2d worldPos{ desiredPosition.x + static_cast<double>(local.localPos.x),
                           desiredPosition.y + static_cast<double>(local.localPos.y) };
        predictedCells.push_back(Vector2i{ static_cast<int>(std::floor(worldPos.x)),
                                           static_cast<int>(std::floor(worldPos.y)) });
    }

    CollisionResult collisionResult = collision_->detect(world, id, currentCells, predictedCells);

    if (!collisionResult.blocked) {
        position = desiredPosition;
    }
    else {
        collision_->respond(collisionResult, velocity);

        // Clamp position to stay within current cell.
        Vector2i currentCell{ static_cast<int>(std::floor(position.x)),
                              static_cast<int>(std::floor(position.y)) };
        constexpr double horizontalMargin = 0.01;

        if (collisionResult.contactNormal.x != 0.0) {
            double cellMinX = static_cast<double>(currentCell.x) + horizontalMargin;
            double cellMaxX = static_cast<double>(currentCell.x + 1) - horizontalMargin;
            position.x = std::clamp(position.x, cellMinX, cellMaxX);
        }

        if (collisionResult.contactNormal.y != 0.0) {
            double cellMinY = static_cast<double>(currentCell.y) + verticalMargin;
            double cellMaxY = static_cast<double>(currentCell.y + 1) - verticalMargin;
            position.y = std::clamp(position.y, cellMinY, cellMaxY);
        }
    }

    // Project to grid.
    projection_->clear(world);
    projection_->project(world, id, position, velocity);

    return RigidBodyUpdateResult{ .on_ground = onGround,
                                  .occupied_cells = projection_->getOccupiedCells() };
}

const std::vector<Vector2i>& RigidBodyComponent::getOccupiedCells() const
{
    return projection_->getOccupiedCells();
}

void RigidBodyComponent::clearProjection(World& world)
{
    projection_->clear(world);
}

PhysicsComponent& RigidBodyComponent::physics()
{
    return *physics_;
}

CollisionComponent& RigidBodyComponent::collision()
{
    return *collision_;
}

ProjectionComponent& RigidBodyComponent::projection()
{
    return *projection_;
}

} // namespace DirtSim
