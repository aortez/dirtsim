#pragma once

#include "DuckSensoryData.h"
#include <random>

namespace DirtSim {

class Duck;

/**
 * Duck actions - what the duck is trying to do.
 */
enum class DuckAction {
    WAIT,      // Stand still.
    RUN_LEFT,  // Run left.
    RUN_RIGHT, // Run right.
    JUMP       // Jump upward.
};

/**
 * Abstract brain interface for duck AI.
 *
 * The brain decides what action the duck should take based on
 * its current state and the world around it.
 */
class DuckBrain {
public:
    virtual ~DuckBrain() = default;

    /**
     * Called each tick to update the brain and get movement intent.
     *
     * The brain should set the duck's velocity based on its decision.
     *
     * @param duck The duck organism.
     * @param sensory Sensory data about the duck's environment.
     * @param deltaTime Time since last update in seconds.
     */
    virtual void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) = 0;

    // Current action state (for debugging/display).
    DuckAction getCurrentAction() const { return current_action_; }

protected:
    DuckAction current_action_ = DuckAction::WAIT;
    float action_timer_ = 0.0f;
};

/**
 * Simple random duck brain - picks random actions.
 *
 * Behavior:
 * - Randomly picks WAIT, RUN_LEFT, RUN_RIGHT, or JUMP.
 * - Runs for a random distance (1-5 cells).
 * - Waits for a random duration (0.5-2 seconds).
 * - Jumps when on ground.
 */
class RandomDuckBrain : public DuckBrain {
public:
    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;

private:
    std::mt19937 rng_{ std::random_device{}() };
    int run_target_cells_ = 0;
    float run_start_x_ = 0.0f;

    void pickNextAction(Duck& duck, const DuckSensoryData& sensory);
};

} // namespace DirtSim
