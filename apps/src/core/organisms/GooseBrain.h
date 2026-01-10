#pragma once

#include "core/Vector2d.h"
#include "core/Vector2i.h"
#include <random>

namespace DirtSim {

class Goose;

/**
 * Goose actions - what the goose is trying to do.
 */
enum class GooseAction {
    WAIT,      // Stand still.
    RUN_LEFT,  // Run left.
    RUN_RIGHT, // Run right.
    JUMP       // Jump upward.
};

/**
 * Sensory data provided to goose brain each tick.
 */
struct GooseSensoryData {
    Vector2i position;      // Current grid position.
    Vector2d velocity;      // Current velocity (from rigid body).
    bool on_ground = false; // Whether goose is on solid ground.
    float facing_x = 1.0f;  // Facing direction (-1 = left, 1 = right).
    double delta_time_seconds = 0.0;
};

/**
 * Abstract brain interface for goose AI.
 *
 * The brain decides what action the goose should take based on
 * its current state and the world around it.
 */
class GooseBrain {
public:
    virtual ~GooseBrain() = default;

    /**
     * Called each tick to update the brain and get movement intent.
     *
     * @param goose The goose organism.
     * @param sensory Sensory data about the goose's environment.
     * @param deltaTime Time since last update in seconds.
     */
    virtual void think(Goose& goose, const GooseSensoryData& sensory, double deltaTime) = 0;

    // Current action state (for debugging/display).
    GooseAction getCurrentAction() const { return current_action_; }

protected:
    GooseAction current_action_ = GooseAction::WAIT;
    float action_timer_ = 0.0f;
};

/**
 * Simple random goose brain - picks random actions.
 */
class RandomGooseBrain : public GooseBrain {
public:
    void think(Goose& goose, const GooseSensoryData& sensory, double deltaTime) override;

private:
    std::mt19937 rng_{ std::random_device{}() };
    int run_target_cells_ = 0;
    float run_start_x_ = 0.0f;

    void pickNextAction(Goose& goose, const GooseSensoryData& sensory);
};

} // namespace DirtSim
