#pragma once

#include "GooseBrain.h"
#include "Organism.h"
#include <memory>

namespace DirtSim {

// Forward declarations for components.
class CollisionComponent;
class PhysicsComponent;
class ProjectionComponent;

/**
 * Goose organism - a mobile creature using rigid body physics.
 *
 * Unlike Duck which uses cell-based physics directly, Goose demonstrates
 * the new organism structural integrity system:
 * - Position stored as Vector2d (continuous space)
 * - Shape defined in local_shape (LocalCell vector)
 * - Projects onto grid each frame via projectToGrid()
 * - Gathers forces from grid, applies to rigid body, then re-projects
 *
 * This approach prevents the "tearing" problem where multi-cell organisms
 * break apart during movement because cells cross grid boundaries at
 * different times.
 *
 * Physics loop:
 * 1. gatherForces() - collect forces from occupied grid cells
 * 2. applyForce() - F=ma integration on rigid body velocity
 * 3. integratePosition() - update position from velocity
 * 4. projectToGrid() - snap position to grid, update cells
 */
class Goose : public Organism {
public:
    /**
     * Construct a new goose with a given brain implementation.
     *
     * @param id Unique organism identifier.
     * @param brain Brain implementation for movement decisions.
     */
    Goose(OrganismId id, std::unique_ptr<GooseBrain> brain);
    ~Goose();

    // Organism interface.
    Vector2i getAnchorCell() const override;
    void setAnchorCell(Vector2i pos) override;
    void update(World& world, double deltaTime) override;
    bool usesRigidBodyPhysics() const override { return true; }

    // Goose-specific state.
    bool isOnGround() const { return on_ground_; }
    GooseAction getCurrentAction() const;

    // Movement control (called by brain).
    void setWalkDirection(float dir);  // -1 = left, 0 = stop, +1 = right.
    void jump();

    // Replace the brain (for testing).
    void setBrain(std::unique_ptr<GooseBrain> brain) { brain_ = std::move(brain); }

private:
    bool on_ground_ = false;
    float walk_direction_ = 0.0f;
    bool jump_requested_ = false;
    uint32_t frame_counter_ = 0;

    std::unique_ptr<GooseBrain> brain_;

    // Physics components.
    std::unique_ptr<CollisionComponent> collision_;
    std::unique_ptr<PhysicsComponent> physics_;
    std::unique_ptr<ProjectionComponent> projection_;

    void updateGroundDetection(const World& world);
    void applyMovementForces(const World& world, double deltaTime);
};

} // namespace DirtSim
