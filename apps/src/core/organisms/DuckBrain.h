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

/**
 * Wall-bouncing duck brain - runs back and forth between walls.
 *
 * Behavior:
 * - Picks the furthest wall from current position.
 * - Runs toward that wall.
 * - When touching the wall, switches to run toward opposite wall.
 * - Repeats indefinitely (ping-pong pattern).
 * - Optional: Learns consistent crossing time and jumps at midpoint.
 */
class WallBouncingBrain : public DuckBrain {
public:
    explicit WallBouncingBrain(bool enable_jumping = false)
        : enable_jumping_(enable_jumping) {}

    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;

private:
    enum class TargetWall {
        LEFT,
        RIGHT
    };

    TargetWall target_wall_ = TargetWall::RIGHT;
    bool initialized_ = false;
    bool enable_jumping_ = false;

    // Timing for jump prediction.
    float current_run_time_ = 0.0f;      // Time elapsed in current run.
    float average_run_time_ = 0.0f;      // Exponential moving average of crossing time.
    int run_count_ = 0;                  // Number of completed runs.
    float jump_timer_ = -1.0f;           // Time until jump (-1 = no jump scheduled).

    void pickFurthestWall(const DuckSensoryData& sensory);
    bool isTouchingWall(const DuckSensoryData& sensory, TargetWall wall) const;
    void onWallTouch(float run_time);
    void updateJumpTimer(Duck& duck, float deltaTime);
};

/**
 * Dead-reckoning duck brain with exit-seeking behavior.
 *
 * On first tick, scans the environment to determine which side has walls
 * (entry side) and heads toward the opposite side (exit). Jumps for fun
 * when in open space between walls.
 *
 * Accumulates elapsed time and displacement internally from per-frame
 * sensory data (delta_time_seconds, velocity).
 */
class DuckBrain2 : public DuckBrain {
public:
    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;

private:
    enum class ExitDirection { LEFT, RIGHT, UNKNOWN };

    ExitDirection exit_direction_ = ExitDirection::UNKNOWN;
    bool initialized_ = false;

    // Accumulated state from per-frame sensory data.
    double elapsed_time_seconds_ = 0.0;
    Vector2d displacement_from_spawn_{ 0.0, 0.0 };
    Vector2i spawn_position_{ 0, 0 };

    // Jump timing.
    float time_since_last_jump_ = 0.0f;
    static constexpr float JUMP_COOLDOWN = 1.5f;

    // Debug logging throttle.
    int debug_frame_counter_ = 0;

    void initialize(const DuckSensoryData& sensory);
    ExitDirection detectExitDirection(const DuckSensoryData& sensory) const;
    bool isTouchingWall(const DuckSensoryData& sensory, ExitDirection side) const;
    bool isInOpenSpace(const DuckSensoryData& sensory) const;
};

} // namespace DirtSim
