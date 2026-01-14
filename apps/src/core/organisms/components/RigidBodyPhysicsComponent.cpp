#include "RigidBodyPhysicsComponent.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <cassert>
#include <cmath>

namespace DirtSim {

RigidBodyPhysicsComponent::RigidBodyPhysicsComponent(Material::EnumType mat) : material(mat)
{}

void RigidBodyPhysicsComponent::addForce(Vector2d force)
{
    pendingForce.x += force.x;
    pendingForce.y += force.y;
}

void RigidBodyPhysicsComponent::applyAirResistance(const World& world, Vector2d velocity)
{
    const double vMag = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    if (vMag < 0.01) {
        return;
    }

    const auto& props = Material::getProperties(material);
    const double strength = world.getAirResistanceStrength();
    const double dragMagnitude = strength * props.air_resistance * vMag * vMag;

    // Force opposes motion.
    pendingForce.x += -velocity.x / vMag * dragMagnitude;
    pendingForce.y += -velocity.y / vMag * dragMagnitude;
}

void RigidBodyPhysicsComponent::clearPendingForce()
{
    pendingForce = { 0.0, 0.0 };
}

void RigidBodyPhysicsComponent::gatherForces(World& world, const std::vector<Vector2i>& cells)
{
    const auto& data = world.getData();

    for (const auto& pos : cells) {
        assert(data.inBounds(pos.x, pos.y) && "Cell position out of bounds.");

        const auto& cell = data.at(pos.x, pos.y);
        pendingForce.x += cell.pending_force.x;
        pendingForce.y += cell.pending_force.y;
    }
}

Vector2d RigidBodyPhysicsComponent::getPendingForce() const
{
    return pendingForce;
}

void RigidBodyPhysicsComponent::integrate(Vector2d& velocity, double mass, double dt)
{
    if (mass < 0.0001) {
        return;
    }

    const Vector2d acceleration{ pendingForce.x / mass, pendingForce.y / mass };
    velocity.x += acceleration.x * dt;
    velocity.y += acceleration.y * dt;
}

} // namespace DirtSim
