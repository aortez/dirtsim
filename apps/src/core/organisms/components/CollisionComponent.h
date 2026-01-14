#pragma once

#include "core/Vector2d.h"
#include "core/Vector2i.h"
#include "core/organisms/OrganismType.h"
#include <vector>

namespace DirtSim {

class World;

/**
 * Result of collision detection for a rigid body organism.
 */
struct CollisionResult {
    bool blocked = false;
    std::vector<Vector2i> blockedCells;
    Vector2d contactNormal{ 0.0, 0.0 };
};

/**
 * Interface for organism collision detection and response.
 */
class CollisionComponent {
public:
    virtual ~CollisionComponent() = default;

    virtual CollisionResult detect(
        const World& world,
        OrganismId organismId,
        const std::vector<Vector2i>& currentCells,
        const std::vector<Vector2i>& predictedCells) = 0;

    virtual void respond(
        const CollisionResult& collision, Vector2d& velocity, double restitution = 0.0) = 0;

    virtual Vector2d computeSupportForce(
        const World& world,
        OrganismId organismId,
        const std::vector<Vector2i>& currentCells,
        double weight,
        Vector2d gravityDir) = 0;

    virtual Vector2d computeGroundFriction(
        const World& world,
        OrganismId organismId,
        const std::vector<Vector2i>& currentCells,
        const Vector2d& velocity,
        double normalForce) = 0;
};

} // namespace DirtSim
