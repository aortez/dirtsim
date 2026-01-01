#pragma once

#include "DuckSensoryData.h"
#include "core/input/GamepadState.h"
#include <optional>
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

    /**
     * Set gamepad input for player-controlled brains.
     *
     * Called by SimRunning before think() for brains that respond to gamepad.
     * Default implementation does nothing (AI brains ignore this).
     *
     * @param state Current gamepad state for this tick.
     */
    virtual void setGamepadInput(const GamepadState& /*state*/) {}

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
 * Dead-reckoning duck brain with wall-bouncing and exit-seeking behavior.
 *
 * Behavior:
 * 1. Knows it spawned on one side of the world (entry side).
 * 2. Runs toward the opposite side until it finds a wall (exit wall).
 * 3. Once exit wall is found, bounces between walls for fun.
 * 4. Jumps in the middle of the world when running fast.
 * 5. When exit door opens (gap in exit wall), overrides and runs to exit.
 *
 * Accumulates elapsed time and displacement internally from per-frame
 * sensory data (delta_time_seconds, velocity).
 */
class DuckBrain2 : public DuckBrain {
public:
    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;

private:
    enum class Side { LEFT, RIGHT, UNKNOWN };
    enum class Phase { SEEKING_EXIT_WALL, BOUNCING, EXITING };

    // Core state.
    bool initialized_ = false;
    Phase phase_ = Phase::SEEKING_EXIT_WALL;
    Side spawn_side_ = Side::UNKNOWN;
    Side current_target_ = Side::UNKNOWN;

    // Accumulated state from per-frame sensory data.
    double elapsed_time_seconds_ = 0.0;
    Vector2d displacement_from_spawn_{ 0.0, 0.0 };
    int spawn_x_ = 0;

    // Wall tracking (recorded x positions).
    int entry_wall_x_ = -1;
    int exit_wall_x_ = -1;
    bool found_exit_wall_ = false;

    // Max speed learning.
    static constexpr double SPEED_CONVERGENCE_MARGIN = 1.0;
    static constexpr double SPEED_CONVERGENCE_TIME = 1.0;
    double last_speed_ = 0.0;
    double steady_speed_time_ = 0.0;
    double max_speed_ = 0.0;
    bool max_speed_learned_ = false;

    // Jump distance learning.
    static constexpr float JUMP_DISTANCE_EMA_ALPHA = 0.3f;
    int jump_start_x_ = -1;
    bool in_jump_ = false;
    double learned_jump_distance_ = 0.0;
    bool jump_distance_learned_ = false;

    // Jump timing.
    static constexpr float JUMP_COOLDOWN = 3.0f;
    static constexpr float MIN_SPEED_FOR_JUMP = 2.0f;
    static constexpr float MIN_SPEED_RATIO_FOR_JUMP = 0.9f;
    float jump_cooldown_seconds_ = 0.0f;

    // Debug logging throttle.
    int debug_frame_counter_ = 0;

    void initialize(const DuckSensoryData& sensory);
    Side detectSpawnSide(const DuckSensoryData& sensory) const;
    bool isTouchingWall(const DuckSensoryData& sensory, Side side) const;
    bool detectsGapInExitWall(const DuckSensoryData& sensory) const;
    bool detectsObstacleAhead(const DuckSensoryData& sensory) const;
    bool isNearMiddle(const DuckSensoryData& sensory) const;
    void setRunDirection(Duck& duck, Side target);
};

} // namespace DirtSim
