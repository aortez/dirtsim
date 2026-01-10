#pragma once

#include "CollisionComponent.h"

namespace DirtSim {

/**
 * Collision component for multi-cell rigid body organisms.
 *
 * Detects collisions with world boundaries, walls, other organisms, and dense
 * solids. Computes contact normal from organism center toward blocking cells.
 */
class RigidBodyCollisionComponent : public CollisionComponent {
public:
    CollisionResult detect(
        const World& world,
        OrganismId organismId,
        const std::vector<Vector2i>& currentCells,
        const std::vector<Vector2i>& predictedCells) override;

    void respond(
        const CollisionResult& collision, Vector2d& velocity, double restitution = 0.0) override;

    Vector2d computeSupportForce(
        const World& world,
        OrganismId organismId,
        const std::vector<Vector2i>& currentCells,
        double weight,
        Vector2d gravityDir) override;

    Vector2d computeGroundFriction(
        const World& world,
        OrganismId organismId,
        const std::vector<Vector2i>& currentCells,
        const Vector2d& velocity,
        double normalForce) override;
};

} // namespace DirtSim
