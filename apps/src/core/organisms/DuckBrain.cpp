#include "DuckBrain.h"
#include "Duck.h"
#include "DuckSensoryData.h"
#include "core/LoggingChannels.h"
#include <cmath>

namespace DirtSim {

void RandomDuckBrain::think(Duck& duck, const DuckSensoryData& sensory, double deltaTime)
{
    // Decrement action timer.
    action_timer_ -= static_cast<float>(deltaTime);

    // Check if current action is complete.
    bool action_complete = false;

    switch (current_action_) {
    case DuckAction::WAIT:
        // Wait action completes when timer expires.
        action_complete = (action_timer_ <= 0.0f);
        duck.setWalkDirection(0.0f);
        break;

    case DuckAction::RUN_LEFT:
    case DuckAction::RUN_RIGHT: {
        // Run action completes when we've traveled the target distance or hit a wall.
        float current_x = static_cast<float>(sensory.position.x);
        float distance_traveled = std::abs(current_x - run_start_x_);

        if (distance_traveled >= run_target_cells_ || action_timer_ <= 0.0f) {
            action_complete = true;
            duck.setWalkDirection(0.0f);
        }
        else {
            // Keep running.
            float direction = (current_action_ == DuckAction::RUN_LEFT) ? -1.0f : 1.0f;
            duck.setWalkDirection(direction);
        }
        break;
    }

    case DuckAction::JUMP:
        // Jump action completes immediately after initiating.
        // The actual jump physics is handled by Duck::jump().
        action_complete = true;
        break;
    }

    // Pick next action if current one is complete.
    if (action_complete) {
        pickNextAction(duck, sensory);
    }
}

void RandomDuckBrain::pickNextAction(Duck& duck, const DuckSensoryData& sensory)
{
    // Weight the actions - more waiting, less jumping.
    std::uniform_int_distribution<int> action_dist(0, 10);
    int roll = action_dist(rng_);

    if (roll < 4) {
        // 40% chance: Wait.
        current_action_ = DuckAction::WAIT;
        std::uniform_real_distribution<float> wait_dist(0.5f, 2.0f);
        action_timer_ = wait_dist(rng_);
        duck.setWalkDirection(0.0f);
        LOG_INFO(Brain, "Duck {}: WAIT for {:.1f}s at ({}, {})",
            duck.getId(), action_timer_, sensory.position.x, sensory.position.y);
    }
    else if (roll < 7) {
        // 30% chance: Run left.
        current_action_ = DuckAction::RUN_LEFT;
        std::uniform_int_distribution<int> dist_dist(1, 5);
        run_target_cells_ = dist_dist(rng_);
        run_start_x_ = static_cast<float>(sensory.position.x);
        action_timer_ = 5.0f; // Timeout.
        LOG_INFO(Brain, "Duck {}: RUN_LEFT {} cells from ({}, {})",
            duck.getId(), run_target_cells_, sensory.position.x, sensory.position.y);
    }
    else if (roll < 10) {
        // 30% chance: Run right.
        current_action_ = DuckAction::RUN_RIGHT;
        std::uniform_int_distribution<int> dist_dist(1, 5);
        run_target_cells_ = dist_dist(rng_);
        run_start_x_ = static_cast<float>(sensory.position.x);
        action_timer_ = 5.0f; // Timeout.
        LOG_INFO(Brain, "Duck {}: RUN_RIGHT {} cells from ({}, {})",
            duck.getId(), run_target_cells_, sensory.position.x, sensory.position.y);
    }
    else {
        // 10% chance: Jump (only if on ground).
        if (sensory.on_ground) {
            current_action_ = DuckAction::JUMP;
            duck.jump();
            LOG_INFO(Brain, "Duck {}: JUMP at ({}, {})",
                duck.getId(), sensory.position.x, sensory.position.y);
        }
        else {
            // Can't jump - wait instead.
            current_action_ = DuckAction::WAIT;
            action_timer_ = 0.3f;
            LOG_INFO(Brain, "Duck {}: Can't jump (not on ground), WAIT at ({}, {})",
                duck.getId(), sensory.position.x, sensory.position.y);
        }
    }
}

void WallBouncingBrain::think(Duck& duck, const DuckSensoryData& sensory, double deltaTime)
{
    float dt = static_cast<float>(deltaTime);

    // Initialize on first run - pick furthest wall.
    if (!initialized_) {
        pickFurthestWall(sensory);
        initialized_ = true;
    }

    // Track time for current run.
    current_run_time_ += dt;

    // Check if we're touching the target wall.
    bool touching = isTouchingWall(sensory, target_wall_);

    // Debug: log wall detection every 30 frames.
    static int debug_frame_counter = 0;
    if (debug_frame_counter++ % 30 == 0) {
        constexpr int CENTER = 4;
        constexpr int WALL_MATERIAL_INDEX = 7;
        double left_wall = sensory.material_histograms[CENTER][CENTER - 1][WALL_MATERIAL_INDEX];
        double right_wall = sensory.material_histograms[CENTER][CENTER + 1][WALL_MATERIAL_INDEX];
        LOG_INFO(Brain, "Duck {}: pos=({},{}), target={}, left_wall={:.2f}, right_wall={:.2f}, touching={}",
            duck.getId(), sensory.position.x, sensory.position.y,
            (target_wall_ == TargetWall::LEFT) ? "LEFT" : "RIGHT",
            left_wall, right_wall, touching);
    }

    if (touching) {
        // Wall touched - record timing and switch direction.
        onWallTouch(current_run_time_);
        current_run_time_ = 0.0f;

        target_wall_ = (target_wall_ == TargetWall::LEFT) ? TargetWall::RIGHT : TargetWall::LEFT;
        LOG_INFO(Brain, "Duck {}: Wall touched at ({},{}), switching to {} wall (run_time={:.2f}s, avg={:.2f}s)",
            duck.getId(), sensory.position.x, sensory.position.y,
            (target_wall_ == TargetWall::LEFT) ? "LEFT" : "RIGHT",
            current_run_time_, average_run_time_);
    }

    // Update jump timer and execute jump if ready.
    if (enable_jumping_) {
        updateJumpTimer(duck, dt);
    }

    // Run toward target wall.
    if (target_wall_ == TargetWall::LEFT) {
        current_action_ = DuckAction::RUN_LEFT;
        duck.setWalkDirection(-1.0f);
    } else {
        current_action_ = DuckAction::RUN_RIGHT;
        duck.setWalkDirection(1.0f);
    }
}

void WallBouncingBrain::pickFurthestWall(const DuckSensoryData& sensory)
{
    // TODO: Determine actual world width from sensory data to pick furthest wall.
    // For now, just start by going right.
    target_wall_ = TargetWall::RIGHT;
    LOG_INFO(Brain, "Duck {}: Starting WallBouncingBrain - targeting RIGHT wall (pos {})",
        0, sensory.position.x);
}

bool WallBouncingBrain::isTouchingWall(const DuckSensoryData& sensory, TargetWall wall) const
{
    // Check material histogram for WALL in adjacent cell.
    // Sensory grid is 9x9, center is at (4, 4).
    // MaterialType::WALL = 7.
    // Note: Out-of-bounds cells are marked as WALL by the sensory system.
    constexpr int CENTER = 4;
    constexpr int WALL_MATERIAL_INDEX = 7;
    constexpr double WALL_THRESHOLD = 0.5; // Consider wall present if >= 50% fill.

    if (wall == TargetWall::LEFT) {
        // Check left neighbor (grid position [4][3]).
        double wall_fill = sensory.material_histograms[CENTER][CENTER - 1][WALL_MATERIAL_INDEX];
        return wall_fill >= WALL_THRESHOLD;
    } else {
        // Check right neighbor (grid position [4][5]).
        double wall_fill = sensory.material_histograms[CENTER][CENTER + 1][WALL_MATERIAL_INDEX];
        return wall_fill >= WALL_THRESHOLD;
    }
}

void WallBouncingBrain::onWallTouch(float run_time)
{
    static constexpr float EMA_ALPHA = 0.3f;
    
    run_count_++;

    // Update exponential moving average.
    if (average_run_time_ == 0.0f) {
        // Seed with first run value.
        average_run_time_ = run_time;
    } else {
        // Exponential moving average: 20% weight to new sample.
        average_run_time_ = EMA_ALPHA * run_time + (1.0f - EMA_ALPHA) * average_run_time_;
    }

    // Check if this run was consistent (within 20% of average).
    if (enable_jumping_ && average_run_time_ > 0.0f) {
        float deviation = std::abs(run_time - average_run_time_) / average_run_time_;
        if (deviation <= 0.20f) {
            // Consistent timing - schedule jump at midpoint.
            jump_timer_ = average_run_time_ / 2.0f;
            LOG_INFO(Brain, "Duck: Consistent run time {:.2f}s (avg {:.2f}s, dev {:.1f}%), scheduling jump at {:.2f}s",
                run_time, average_run_time_, deviation * 100.0f, jump_timer_);
        } else {
            LOG_INFO(Brain, "Duck: Inconsistent run time {:.2f}s (avg {:.2f}s, dev {:.1f}%), no jump scheduled",
                run_time, average_run_time_, deviation * 100.0f);
        }
    }
}

void WallBouncingBrain::updateJumpTimer(Duck& duck, float deltaTime)
{
    if (jump_timer_ > 0.0f) {
        jump_timer_ -= deltaTime;

        // Execute jump when timer expires.
        if (jump_timer_ <= 0.0f) {
            duck.jump();
            current_action_ = DuckAction::JUMP;
            LOG_INFO(Brain, "Duck {}: Midpoint jump executed!", duck.getId());
            jump_timer_ = -1.0f; // Reset timer.
        }
    }
}

// ============================================================================
// DuckBrain2 - Dead reckoning with exit-seeking behavior
// ============================================================================

void DuckBrain2::think(Duck& duck, const DuckSensoryData& sensory, double deltaTime)
{
    float dt = static_cast<float>(deltaTime);

    // Accumulate time from per-frame delta.
    elapsed_time_seconds_ += sensory.delta_time_seconds;

    // Accumulate displacement from velocity.
    displacement_from_spawn_.x += sensory.velocity.x * sensory.delta_time_seconds;
    displacement_from_spawn_.y += sensory.velocity.y * sensory.delta_time_seconds;

    // Initialize on first tick.
    if (!initialized_) {
        initialize(sensory);
    }

    // Update jump cooldown.
    time_since_last_jump_ += dt;

    // Check if we're touching the exit wall.
    bool at_exit_wall = isTouchingWall(sensory, exit_direction_);
    bool at_entry_wall = isTouchingWall(sensory,
        exit_direction_ == ExitDirection::LEFT ? ExitDirection::RIGHT : ExitDirection::LEFT);

    // Jump for fun when in open space (not touching either wall).
    bool in_open_space = isInOpenSpace(sensory);
    if (in_open_space && sensory.on_ground && time_since_last_jump_ >= JUMP_COOLDOWN) {
        duck.jump();
        time_since_last_jump_ = 0.0f;
        current_action_ = DuckAction::JUMP;
        LOG_INFO(Brain, "Duck {}: Fun jump in open space! elapsed={:.1f}s, displacement=({:.1f}, {:.1f})",
            duck.getId(), elapsed_time_seconds_, displacement_from_spawn_.x, displacement_from_spawn_.y);
    }

    // Head toward exit direction.
    if (exit_direction_ == ExitDirection::LEFT) {
        current_action_ = DuckAction::RUN_LEFT;
        duck.setWalkDirection(-1.0f);
    }
    else if (exit_direction_ == ExitDirection::RIGHT) {
        current_action_ = DuckAction::RUN_RIGHT;
        duck.setWalkDirection(1.0f);
    }
    else {
        // Unknown direction - just wait.
        current_action_ = DuckAction::WAIT;
        duck.setWalkDirection(0.0f);
    }

    // Debug logging every 60 frames.
    if (debug_frame_counter_++ % 60 == 0) {
        LOG_INFO(Brain, "Duck {}: Brain2 - exit={}, elapsed={:.1f}s, disp=({:.1f},{:.1f}), at_exit={}, at_entry={}, open={}",
            duck.getId(),
            exit_direction_ == ExitDirection::LEFT ? "LEFT" : (exit_direction_ == ExitDirection::RIGHT ? "RIGHT" : "?"),
            elapsed_time_seconds_,
            displacement_from_spawn_.x, displacement_from_spawn_.y,
            at_exit_wall, at_entry_wall, in_open_space);
    }
}

void DuckBrain2::initialize(const DuckSensoryData& sensory)
{
    spawn_position_ = sensory.position;
    exit_direction_ = detectExitDirection(sensory);
    initialized_ = true;

    LOG_INFO(Brain, "DuckBrain2: Initialized at ({}, {}), exit direction = {}",
        spawn_position_.x, spawn_position_.y,
        exit_direction_ == ExitDirection::LEFT ? "LEFT" : (exit_direction_ == ExitDirection::RIGHT ? "RIGHT" : "UNKNOWN"));
}

DuckBrain2::ExitDirection DuckBrain2::detectExitDirection(const DuckSensoryData& sensory) const
{
    // Scan the left and right edges of the 9x9 sensory grid for walls.
    // Entry side will have more wall presence (world boundary).
    // Exit is the opposite direction.
    constexpr int WALL_MATERIAL_INDEX = 7;
    constexpr int CENTER_Y = 4;

    double left_wall_total = 0.0;
    double right_wall_total = 0.0;

    // Check several rows near center for wall presence on edges.
    for (int dy = -2; dy <= 2; dy++) {
        int y = CENTER_Y + dy;
        if (y >= 0 && y < DuckSensoryData::GRID_SIZE) {
            // Left edge (column 0).
            left_wall_total += sensory.material_histograms[y][0][WALL_MATERIAL_INDEX];
            // Right edge (column 8).
            right_wall_total += sensory.material_histograms[y][DuckSensoryData::GRID_SIZE - 1][WALL_MATERIAL_INDEX];
        }
    }

    LOG_INFO(Brain, "DuckBrain2: Wall detection - left_total={:.2f}, right_total={:.2f}",
        left_wall_total, right_wall_total);

    // Entry side has more wall (world boundary). Exit is opposite.
    if (left_wall_total > right_wall_total + 0.5) {
        // Entered from left, exit is right.
        return ExitDirection::RIGHT;
    }
    else if (right_wall_total > left_wall_total + 0.5) {
        // Entered from right, exit is left.
        return ExitDirection::LEFT;
    }
    else {
        // Can't determine - default to right.
        return ExitDirection::RIGHT;
    }
}

bool DuckBrain2::isTouchingWall(const DuckSensoryData& sensory, ExitDirection side) const
{
    constexpr int CENTER = 4;
    constexpr int WALL_MATERIAL_INDEX = 7;
    constexpr double WALL_THRESHOLD = 0.5;

    if (side == ExitDirection::LEFT) {
        double wall_fill = sensory.material_histograms[CENTER][CENTER - 1][WALL_MATERIAL_INDEX];
        return wall_fill >= WALL_THRESHOLD;
    }
    else if (side == ExitDirection::RIGHT) {
        double wall_fill = sensory.material_histograms[CENTER][CENTER + 1][WALL_MATERIAL_INDEX];
        return wall_fill >= WALL_THRESHOLD;
    }
    return false;
}

bool DuckBrain2::isInOpenSpace(const DuckSensoryData& sensory) const
{
    // Open space = not touching wall on either side.
    return !isTouchingWall(sensory, ExitDirection::LEFT) &&
           !isTouchingWall(sensory, ExitDirection::RIGHT);
}

} // namespace DirtSim
