#pragma once

#include "DuckInput.h"
#include "DuckSensoryData.h"
#include "core/input/GamepadState.h"
#include <optional>
#include <random>

namespace DirtSim {

class Duck;

/**
 * Accumulated knowledge that DuckBrain2 learns over time.
 *
 * These are persistent facts that only get updated, never reset.
 * Uses std::optional for values that aren't known yet.
 */
struct DuckKnowledge {
    enum class Side { LEFT, RIGHT, UNKNOWN };

    // Spatial knowledge (learned once at initialization or on discovery).
    Side spawn_side = Side::UNKNOWN;
    int spawn_x = 0;
    int entry_wall_x = -1;
    std::optional<int> exit_wall_x;

    // Self-knowledge (learned through experience).
    std::optional<double> max_speed;     // Learned when velocity converges.
    std::optional<double> jump_distance; // Learned from landing after jumps.

    // Helpers.
    bool knowsMaxSpeed() const { return max_speed.has_value(); }
    bool knowsJumpDistance() const { return jump_distance.has_value(); }
    bool knowsExitWall() const { return exit_wall_x.has_value(); }

    Side exitSide() const
    {
        return (spawn_side == Side::LEFT) ? Side::RIGHT
            : (spawn_side == Side::RIGHT) ? Side::LEFT
                                          : Side::UNKNOWN;
    }
};

/**
 * Per-tick situational assessment for DuckBrain2.
 *
 * Computed fresh each frame from sensory data. Not persisted.
 */
struct DuckSituation {
    // Physical state.
    bool on_ground = false;
    double current_speed = 0.0;
    int facing_direction = 0; // -1 = left, +1 = right, 0 = none.

    // Spatial awareness (what's ahead).
    bool wall_ahead = false;
    int obstacle_distance = -1; // Distance to obstacle in cells, -1 if none detected.
    bool cliff_ahead = false;
    bool gap_in_exit_wall = false;

    // Derived assessments (require knowledge to compute).
    bool near_middle = false;
    bool at_full_speed = false;   // True if current_speed >= 90% of known max_speed.
    bool can_clear_cliff = false; // True if jump_distance would clear detected cliff.

    // Convenience accessor.
    bool obstacle_ahead() const { return obstacle_distance >= 0; }
};

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
    explicit WallBouncingBrain(bool enable_jumping = false) : enable_jumping_(enable_jumping) {}

    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;

private:
    enum class TargetWall { LEFT, RIGHT };

    TargetWall target_wall_ = TargetWall::RIGHT;
    bool initialized_ = false;
    bool enable_jumping_ = false;

    // Timing for jump prediction.
    float current_run_time_ = 0.0f; // Time elapsed in current run.
    float average_run_time_ = 0.0f; // Exponential moving average of crossing time.
    int run_count_ = 0;             // Number of completed runs.
    float jump_timer_ = -1.0f;      // Time until jump (-1 = no jump scheduled).

    void pickFurthestWall(const DuckSensoryData& sensory);
    bool isTouchingWall(const DuckSensoryData& sensory, TargetWall wall) const;
    void onWallTouch(float run_time);
    bool shouldJump(float deltaTime);
};

/**
 * Dead-reckoning duck brain with wall-bouncing and exit-seeking behavior.
 *
 * Architecture:
 * - Goal (Phase): High-level objective (SEEKING_EXIT_WALL → BOUNCING → EXITING).
 * - Knowledge: Accumulated facts about self and world (max_speed, jump_distance, wall positions).
 * - Situation: Per-tick assessment of current conditions (cliff_ahead, at_full_speed, etc.).
 *
 * Behavior:
 * 1. Knows it spawned on one side of the world (entry side).
 * 2. Runs toward the opposite side until it finds a wall (exit wall).
 * 3. Once exit wall is found, bounces between walls for fun.
 * 4. Jumps in the middle of the world when running fast.
 * 5. Jumps over cliffs when it knows it can clear them.
 * 6. When exit door opens (gap in exit wall), overrides and runs to exit.
 */
class DuckBrain2 : public DuckBrain {
public:
    using Side = DuckKnowledge::Side;

    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;

    // Accessors for testing/debugging.
    const DuckKnowledge& getKnowledge() const { return knowledge_; }

private:
    // Goal state machine.
    enum class Phase { SEEKING_EXIT_WALL, BOUNCING, EXITING };

    Phase phase_ = Phase::SEEKING_EXIT_WALL;
    Side current_target_ = Side::UNKNOWN;
    bool initialized_ = false;

    // Accumulated knowledge (persistent).
    DuckKnowledge knowledge_;

    // Learning state (transient, used to build knowledge).
    static constexpr double SPEED_CONVERGENCE_MARGIN = 1.0;
    static constexpr double SPEED_CONVERGENCE_TIME = 1.0;
    double last_speed_ = 0.0;
    double steady_speed_time_ = 0.0;

    static constexpr float JUMP_DISTANCE_EMA_ALPHA = 0.3f;
    int jump_start_x_ = -1;
    bool in_jump_ = false;

    // Jump timing.
    static constexpr float JUMP_COOLDOWN = 3.0f;
    static constexpr float MIN_SPEED_FOR_JUMP = 2.0f;
    static constexpr float MIN_SPEED_RATIO_FOR_JUMP = 0.9f;
    static constexpr float MIN_SPEED_RATIO_FOR_CLIFF_JUMP = 0.2f;
    float jump_cooldown_seconds_ = 0.0f;

    // Tracking.
    double elapsed_time_seconds_ = 0.0;
    int debug_frame_counter_ = 0;

    // Initialization.
    void initialize(const DuckSensoryData& sensory);
    Side detectSpawnSide(const DuckSensoryData& sensory) const;

    // Situation assessment (computed fresh each tick).
    DuckSituation assessSituation(const DuckSensoryData& sensory) const;
    bool isTouchingWall(const DuckSensoryData& sensory, Side side) const;
    bool detectsGapInExitWall(const DuckSensoryData& sensory) const;
    int findObstacleDistance(const DuckSensoryData& sensory) const;
    bool detectsCliffAhead(const DuckSensoryData& sensory) const;
    bool isNearMiddle(const DuckSensoryData& sensory) const;

    // Learning (updates knowledge_).
    void updateSpeedLearning(const DuckSensoryData& sensory);
    void updateJumpDistanceLearning(const DuckSensoryData& sensory);

    // Actions.
    float moveForSide(Side side) const;
};

} // namespace DirtSim
