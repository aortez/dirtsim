#pragma once

#include "core/Vector2d.h"
#include "core/Vector2i.h"
#include <vector>

namespace DirtSim {

class World;

/**
 * Interface for organism physics behavior.
 *
 * Different organisms use different physics strategies:
 * - RigidBodyPhysicsComponent: Multi-cell organisms that move as one unit.
 * - CellPhysicsComponent: Single-cell organisms that delegate to world physics.
 */
class PhysicsComponent {
public:
    virtual ~PhysicsComponent() = default;

    virtual void addForce(Vector2d force) = 0;
    virtual void applyAirResistance(const World& world, Vector2d velocity) = 0;
    virtual void clearPendingForce() = 0;
    virtual void gatherForces(World& world, const std::vector<Vector2i>& cells) = 0;
    virtual Vector2d getPendingForce() const = 0;
    virtual void integrate(Vector2d& velocity, double mass, double dt) = 0;
};

} // namespace DirtSim
