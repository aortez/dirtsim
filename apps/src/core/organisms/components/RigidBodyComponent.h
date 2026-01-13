/**
 * Composite component that orchestrates rigid body physics for organisms.
 *
 * Owns and coordinates PhysicsComponent, CollisionComponent, and ProjectionComponent.
 * Organisms call update() each frame with external forces; the component handles the
 * full physics loop: support, friction, forces, integration, collision, projection.
 */
#pragma once

#include "core/MaterialType.h"
#include "core/Vector2d.h"
#include "core/Vector2i.h"
#include "core/organisms/OrganismType.h"
#include <memory>
#include <vector>

namespace DirtSim {

class CollisionComponent;
class PhysicsComponent;
class ProjectionComponent;
class World;
struct LocalCell;

struct RigidBodyUpdateResult {
    bool on_ground = false;
    std::vector<Vector2i> occupied_cells;
};

class RigidBodyComponent {
public:
    explicit RigidBodyComponent(Material::EnumType material);
    ~RigidBodyComponent();

    RigidBodyComponent(RigidBodyComponent&&) noexcept;
    RigidBodyComponent& operator=(RigidBodyComponent&&) noexcept;

    void addCell(Vector2i localPos, Material::EnumType material, double fillRatio);

    RigidBodyUpdateResult update(
        OrganismId id,
        Vector2d& position,
        Vector2d& velocity,
        double mass,
        const std::vector<LocalCell>& localShape,
        World& world,
        double deltaTime,
        Vector2d externalForce = { 0.0, 0.0 },
        double verticalMargin = 0.01);

    const std::vector<Vector2i>& getOccupiedCells() const;
    void clearProjection(World& world);

    // Sub-component access for testing.
    PhysicsComponent& physics();
    CollisionComponent& collision();
    ProjectionComponent& projection();

private:
    std::unique_ptr<PhysicsComponent> physics_;
    std::unique_ptr<CollisionComponent> collision_;
    std::unique_ptr<ProjectionComponent> projection_;
};

} // namespace DirtSim
