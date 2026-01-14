#pragma once

#include "PhysicsComponent.h"
#include "core/MaterialType.h"

namespace DirtSim {

/**
 * Physics component for multi-cell rigid body organisms.
 *
 * Gathers forces from occupied grid cells, applies air resistance, and
 * integrates using F=ma. All cells move together via unified velocity.
 */
class RigidBodyPhysicsComponent : public PhysicsComponent {
public:
    explicit RigidBodyPhysicsComponent(Material::EnumType material = Material::EnumType::Wood);

    void addForce(Vector2d force) override;
    void applyAirResistance(const World& world, Vector2d velocity) override;
    void clearPendingForce() override;
    void gatherForces(World& world, const std::vector<Vector2i>& cells) override;
    Vector2d getPendingForce() const override;
    void integrate(Vector2d& velocity, double mass, double dt) override;

    Material::EnumType material = Material::EnumType::Wood;
    Vector2d pendingForce{ 0.0, 0.0 };
};

} // namespace DirtSim
