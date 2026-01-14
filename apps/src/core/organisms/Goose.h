#pragma once

#include "Body.h"
#include "GooseBrain.h"
#include <memory>

namespace DirtSim {

class RigidBodyComponent;

/**
 * Goose organism - a mobile creature using rigid body physics.
 *
 * Uses RigidBodyComponent for physics, collision, and grid projection.
 * Brain decides movement; physics handles the rest.
 */
class Goose : public Organism::Body {
public:
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
    void setWalkDirection(float dir);
    void jump();

    void setBrain(std::unique_ptr<GooseBrain> brain) { brain_ = std::move(brain); }

private:
    bool on_ground_ = false;
    float walk_direction_ = 0.0f;
    bool jump_requested_ = false;
    uint32_t frame_counter_ = 0;

    std::unique_ptr<GooseBrain> brain_;
    std::unique_ptr<RigidBodyComponent> rigidBody_;
};

} // namespace DirtSim
