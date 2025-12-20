#pragma once

#include "DuckBrain.h"
#include "DuckSensoryData.h"
#include "Organism.h"
#include <memory>
#include <random>
#include <vector>

namespace DirtSim {

/**
 * Sparkle particle owned by a duck.
 *
 * Sparkles spawn at the duck, receive random force impulses,
 * and fade out over their lifetime.
 */
struct DuckSparkle {
    Vector2<float> position{ 0.0f, 0.0f };  // Absolute world position.
    Vector2<float> velocity{ 0.0f, 0.0f };  // Cells per second.
    float lifetime = 1.0f;                   // Remaining life (1.0 = full, 0.0 = dead).
    float max_lifetime = 1.0f;               // For calculating opacity.
};

/**
 * Duck organism - a mobile creature that walks, jumps, and runs.
 *
 * The duck is represented as a single WOOD cell in the physics simulation.
 * It participates fully in cell physics (gravity, collisions, friction)
 * while the brain controls its intended movement.
 *
 * Physics approach:
 * - Duck occupies a WOOD cell (organism_id marks it as duck).
 * - Brain sets velocity intent on the cell.
 * - World physics handles gravity, collisions, friction.
 * - Duck checks surrounding cells for ground detection.
 *
 * Rendering:
 * - UI draws duck sprite at the duck's cell position.
 * - Sprite flips based on facing direction.
 */
class Duck : public Organism {
public:
    /**
     * Construct a new duck with a given brain implementation.
     *
     * @param id Unique organism identifier.
     * @param brain Brain implementation for movement decisions.
     */
    Duck(OrganismId id, std::unique_ptr<DuckBrain> brain);

    // Organism interface.
    Vector2i getAnchorCell() const override { return anchor_cell_; }
    void setAnchorCell(Vector2i pos) override { anchor_cell_ = pos; }
    void update(World& world, double deltaTime) override;

    // Duck-specific state.
    bool isOnGround() const { return on_ground_; }
    DuckAction getCurrentAction() const;

    // Movement control (called by brain).
    void setWalkDirection(float dir);  // -1 = left, 0 = stop, +1 = right.
    void jump();

    // Replace the brain (for testing).
    void setBrain(std::unique_ptr<DuckBrain> brain) { brain_ = std::move(brain); }

    // Sparkle access for rendering.
    const std::vector<DuckSparkle>& getSparkles() const { return sparkles_; }

    // Sensory data gathering for brain decisions.
    DuckSensoryData gatherSensoryData(const World& world) const;

private:
    Vector2i anchor_cell_{ 0, 0 };
    bool on_ground_ = false;
    float walk_direction_ = 0.0f;  // -1 = left, 0 = stop, +1 = right.
    bool jump_requested_ = false;  // Set by jump(), cleared after force applied.
    uint32_t frame_counter_ = 0;

    std::unique_ptr<DuckBrain> brain_;

    // Sparkle particle system.
    std::vector<DuckSparkle> sparkles_;
    std::mt19937 sparkle_rng_{ std::random_device{}() };
    Vector2d previous_velocity_{ 0.0, 0.0 };  // For acceleration calculation.
    Vector2d smoothed_acceleration_{ 0.0, 0.0 };  // Smoothed X/Y acceleration for sparkle emission.

    void updateGroundDetection(const World& world);
    void applyMovementToCell(World& world, double deltaTime);
    void logPhysicsState(const World& world);
    void updateSparkles(const World& world, double deltaTime);
    void spawnSparkle(const Vector2d& duck_velocity);
    bool isSolidCell(const World& world, int x, int y) const;
    int getDesiredSparkleCount(float acceleration) const;
};

} // namespace DirtSim
