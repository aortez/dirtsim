#include "DuckBrain.h"
#include "Duck.h"
#include "DuckSensoryData.h"
#include "core/LoggingChannels.h"
#include "core/organisms/OrganismSensoryData.h"
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
// DuckBrain2 - Dead reckoning with wall-bouncing and exit-seeking behavior
// ============================================================================

void DuckBrain2::think(Duck& duck, const DuckSensoryData& sensory, double deltaTime)
{
    float dt = static_cast<float>(deltaTime);

    // Accumulate time and displacement from per-frame data.
    elapsed_time_seconds_ += sensory.delta_time_seconds;
    displacement_from_spawn_.x += sensory.velocity.x * sensory.delta_time_seconds;
    displacement_from_spawn_.y += sensory.velocity.y * sensory.delta_time_seconds;

    // Initialize on first tick.
    if (!initialized_) {
        initialize(sensory);
    }

    // Update jump cooldown.
    jump_cooldown_seconds_ -= dt;

    // Track jump state and learn jump distance.
    bool was_in_jump = in_jump_;
    in_jump_ = !sensory.on_ground;

    // Jump started (just left ground).
    if (!was_in_jump && in_jump_) {
        jump_start_x_ = sensory.position.x;
    }

    // Jump ended (just landed).
    if (was_in_jump && !in_jump_ && jump_start_x_ >= 0) {
        int jump_distance = std::abs(sensory.position.x - jump_start_x_);

        // Update learned jump distance with exponential moving average.
        if (!jump_distance_learned_) {
            learned_jump_distance_ = static_cast<double>(jump_distance);
            jump_distance_learned_ = true;
            LOG_INFO(Brain, "Duck {}: First jump distance = {} cells", duck.getId(), jump_distance);
        } else {
            learned_jump_distance_ = JUMP_DISTANCE_EMA_ALPHA * jump_distance
                + (1.0f - JUMP_DISTANCE_EMA_ALPHA) * learned_jump_distance_;
            LOG_INFO(Brain, "Duck {}: Jump distance = {} cells, EMA = {:.1f} cells",
                duck.getId(), jump_distance, learned_jump_distance_);
        }

        jump_start_x_ = -1;
    }

    // Learn max speed through convergence detection.
    if (!max_speed_learned_) {
        double current_speed = std::abs(sensory.velocity.x);

        // Check if speed is steady (within margin of last speed).
        if (std::abs(current_speed - last_speed_) < SPEED_CONVERGENCE_MARGIN) {
            steady_speed_time_ += sensory.delta_time_seconds;

            // Lock in max speed after 1 second of steady state.
            if (steady_speed_time_ >= SPEED_CONVERGENCE_TIME) {
                max_speed_ = current_speed;
                max_speed_learned_ = true;
                LOG_INFO(Brain, "Duck {}: Learned max speed = {:.1f} cells/sec (converged for {:.1f}s)",
                    duck.getId(), max_speed_, steady_speed_time_);
            }
        } else {
            // Speed changed - reset steady state timer.
            steady_speed_time_ = 0.0;
        }

        last_speed_ = current_speed;
    }

    // Detect walls.
    Side exit_side = (spawn_side_ == Side::LEFT) ? Side::RIGHT : Side::LEFT;

    // Priority 1: Check if exit door is visible - override everything.
    if (found_exit_wall_ && phase_ == Phase::BOUNCING) {
        if (detectsGapInExitWall(sensory)) {
            // Debug: show sensory grid when gap detected.
            constexpr int WALL_IDX = 7;
            std::string grid_str;
            for (int col = 0; col < DuckSensoryData::GRID_SIZE; ++col) {
                double wall_fill = sensory.material_histograms[4][col][WALL_IDX];
                grid_str += (wall_fill >= 0.5) ? "W" : ".";
            }
            LOG_INFO(Brain, "Duck {}: Detected gap in exit wall at pos={}, exit_wall_x={}. Grid: [{}]. Switching to EXITING phase.",
                duck.getId(), sensory.position.x, exit_wall_x_, grid_str);
            phase_ = Phase::EXITING;
        }
    }
    bool touching_exit_wall = isTouchingWall(sensory, exit_side);
    bool touching_entry_wall = isTouchingWall(sensory, spawn_side_);

    // Phase-specific behavior.
    switch (phase_) {
    case Phase::SEEKING_EXIT_WALL: {
        // Run toward exit side until we find the wall boundary pattern.
        setRunDirection(duck, exit_side);

        // Debug: log sensory grid every 60 frames during seeking.
        if (debug_frame_counter_ % 60 == 0) {
            constexpr int WALL_IDX = 7;
            std::string grid_str;
            for (int row = 0; row < DuckSensoryData::GRID_SIZE; ++row) {
                for (int col = 0; col < DuckSensoryData::GRID_SIZE; ++col) {
                    double wall_fill = sensory.material_histograms[row][col][WALL_IDX];
                    grid_str += (wall_fill >= 0.5) ? "W" : ".";
                }
                grid_str += "\n";
            }
            LOG_INFO(Brain, "Duck {}: SEEKING at pos={}, sensory grid:\n{}", duck.getId(), sensory.position.x, grid_str);
        }

        // Build wall boundary template: vertical wall with floor.
        SensoryUtils::SensoryTemplate wall_template(2, 4);
        int wall_col = (exit_side == Side::RIGHT) ? 1 : 0;
        int empty_col = (exit_side == Side::RIGHT) ? 0 : 1;

        // Rows 0-2: vertical wall + empty.
        for (int row = 0; row < 3; ++row) {
            wall_template.pattern[row][wall_col] = SensoryUtils::CellPattern(
                SensoryUtils::MatchMode::Is, {MaterialType::WALL});
            wall_template.pattern[row][empty_col] = SensoryUtils::CellPattern(
                SensoryUtils::MatchMode::IsEmpty);
        }
        // Row 3: floor (both columns are wall).
        wall_template.pattern[3][0] = SensoryUtils::CellPattern(
            SensoryUtils::MatchMode::Is, {MaterialType::WALL});
        wall_template.pattern[3][1] = SensoryUtils::CellPattern(
            SensoryUtils::MatchMode::Is, {MaterialType::WALL});

        // Find wall boundary pattern.
        auto match = SensoryUtils::findTemplate<DuckSensoryData::GRID_SIZE, DuckSensoryData::NUM_MATERIALS>(
            sensory.material_histograms, wall_template);

        if (match.found) {
            // Calculate actual wall position from grid match.
            int wall_grid_col = match.col + wall_col;
            exit_wall_x_ = sensory.world_offset.x + wall_grid_col;
            found_exit_wall_ = true;
            phase_ = Phase::BOUNCING;
            current_target_ = spawn_side_;
            jump_cooldown_seconds_ = JUMP_COOLDOWN;
            LOG_INFO(Brain, "Duck {}: Found exit wall boundary at grid col {}, world x={}. Starting BOUNCING phase.",
                duck.getId(), wall_grid_col, exit_wall_x_);
        }
        break;
    }

    case Phase::BOUNCING:
        // Bounce between walls, jumping in the middle when moving fast.
        setRunDirection(duck, current_target_);

        // Check if we hit a wall and need to turn around.
        if (current_target_ == spawn_side_ && touching_entry_wall) {
            current_target_ = exit_side;
            jump_cooldown_seconds_ = 0.0f;
            LOG_INFO(Brain, "Duck {}: Hit entry wall, bouncing toward exit.", duck.getId());
        }
        else if (current_target_ == exit_side && touching_exit_wall) {
            current_target_ = spawn_side_;
            jump_cooldown_seconds_ = 0.0f;
            LOG_INFO(Brain, "Duck {}: Hit exit wall, bouncing toward entry.", duck.getId());
        }

        // Jump over obstacles or in the middle when running fast!
        if (sensory.on_ground && jump_cooldown_seconds_ <= 0.0f) {
            double current_speed = std::abs(sensory.velocity.x);

            // Determine if we're going fast enough to jump.
            bool fast_enough = false;
            if (max_speed_learned_) {
                // Use learned max speed: must be at 90% or higher.
                fast_enough = (current_speed >= max_speed_ * MIN_SPEED_RATIO_FOR_JUMP);
            } else {
                // Fallback: use absolute threshold.
                fast_enough = (current_speed >= MIN_SPEED_FOR_JUMP);
            }

            if (fast_enough) {
                // Jump if obstacle ahead or in middle zone.
                bool has_obstacle = detectsObstacleAhead(sensory);
                bool in_middle = isNearMiddle(sensory);

                if (has_obstacle || in_middle) {
                    duck.jump();
                    jump_cooldown_seconds_ = JUMP_COOLDOWN;
                    current_action_ = DuckAction::JUMP;
                    const char* reason = has_obstacle ? "obstacle" : "middle";
                    LOG_INFO(Brain, "Duck {}: Jumping over {}! speed={:.1f}, max_speed={:.1f}, ratio={:.2f}, pos={}, obstacle={}, middle={}",
                        duck.getId(), reason, current_speed, max_speed_,
                        max_speed_learned_ ? (current_speed / max_speed_) : 0.0,
                        sensory.position.x, has_obstacle, in_middle);
                }
            }
        }
        break;

    case Phase::EXITING:
        // Run toward exit as fast as possible.
        setRunDirection(duck, exit_side);
        break;
    }

    // Debug logging every 60 frames.
    if (debug_frame_counter_++ % 60 == 0) {
        const char* phase_str = (phase_ == Phase::SEEKING_EXIT_WALL) ? "SEEKING" :
                                (phase_ == Phase::BOUNCING) ? "BOUNCING" : "EXITING";
        const char* spawn_str = (spawn_side_ == Side::LEFT) ? "L" : "R";
        LOG_INFO(Brain, "Duck {}: phase={}, spawn={}, exit_wall_x={}, pos={}, vel={:.1f}, elapsed={:.1f}s",
            duck.getId(), phase_str, spawn_str, exit_wall_x_,
            sensory.position.x, sensory.velocity.x, elapsed_time_seconds_);
    }
}

void DuckBrain2::initialize(const DuckSensoryData& sensory)
{
    spawn_x_ = sensory.position.x;
    spawn_side_ = detectSpawnSide(sensory);
    entry_wall_x_ = sensory.position.x;  // We're next to entry wall at spawn.
    current_target_ = (spawn_side_ == Side::LEFT) ? Side::RIGHT : Side::LEFT;
    initialized_ = true;

    LOG_INFO(Brain, "DuckBrain2: Initialized at x={}, spawn_side={}, heading {}",
        spawn_x_,
        spawn_side_ == Side::LEFT ? "LEFT" : "RIGHT",
        current_target_ == Side::LEFT ? "LEFT" : "RIGHT");
}

DuckBrain2::Side DuckBrain2::detectSpawnSide(const DuckSensoryData& sensory) const
{
    // Check which side has more wall - that's where we spawned.
    constexpr int WALL_MATERIAL_INDEX = 7;
    constexpr int CENTER_Y = 4;

    double left_wall_total = 0.0;
    double right_wall_total = 0.0;

    // Check several rows for wall presence on edges of sensory grid.
    for (int dy = -2; dy <= 2; dy++) {
        int y = CENTER_Y + dy;
        if (y >= 0 && y < DuckSensoryData::GRID_SIZE) {
            left_wall_total += sensory.material_histograms[y][0][WALL_MATERIAL_INDEX];
            right_wall_total += sensory.material_histograms[y][DuckSensoryData::GRID_SIZE - 1][WALL_MATERIAL_INDEX];
        }
    }

    LOG_INFO(Brain, "DuckBrain2: Spawn detection - left_wall={:.2f}, right_wall={:.2f}",
        left_wall_total, right_wall_total);

    // Spawn side has more wall presence.
    if (left_wall_total > right_wall_total + 0.5) {
        return Side::LEFT;
    }
    else if (right_wall_total > left_wall_total + 0.5) {
        return Side::RIGHT;
    }
    else {
        // Can't determine - default based on position.
        return Side::LEFT;
    }
}

bool DuckBrain2::isTouchingWall(const DuckSensoryData& sensory, Side side) const
{
    constexpr int CENTER = 4;
    constexpr int WALL_MATERIAL_INDEX = 7;
    constexpr double WALL_THRESHOLD = 0.5;

    if (side == Side::LEFT) {
        double wall_fill = sensory.material_histograms[CENTER][CENTER - 1][WALL_MATERIAL_INDEX];
        return wall_fill >= WALL_THRESHOLD;
    }
    else if (side == Side::RIGHT) {
        double wall_fill = sensory.material_histograms[CENTER][CENTER + 1][WALL_MATERIAL_INDEX];
        return wall_fill >= WALL_THRESHOLD;
    }
    return false;
}

bool DuckBrain2::detectsGapInExitWall(const DuckSensoryData& sensory) const
{
    // Check if there's an opening where we previously recorded the exit wall.
    // Door template: empty above wall.

    // Calculate where the exit wall appears in our current sensory grid.
    int exit_wall_grid_x = exit_wall_x_ - sensory.world_offset.x;

    // Exit wall must be visible in our sensory grid.
    if (exit_wall_grid_x < 0 || exit_wall_grid_x >= DuckSensoryData::GRID_SIZE) {
        return false;
    }

    // Build door template: empty opening above floor.
    SensoryUtils::SensoryTemplate door_template(1, 2);
    door_template.pattern[0][0] = SensoryUtils::CellPattern(SensoryUtils::MatchMode::IsEmpty);
    door_template.pattern[1][0] = SensoryUtils::CellPattern(SensoryUtils::MatchMode::Is, {MaterialType::WALL});

    // Check around center row for door pattern at the recorded wall position.
    constexpr int CENTER_Y = 4;
    for (int check_y = CENTER_Y - 1; check_y <= CENTER_Y + 1; ++check_y) {
        if (SensoryUtils::matchesTemplate<DuckSensoryData::GRID_SIZE, DuckSensoryData::NUM_MATERIALS>(
                sensory.material_histograms, door_template, exit_wall_grid_x, check_y)) {
            return true;
        }
    }

    return false;
}

bool DuckBrain2::detectsObstacleAhead(const DuckSensoryData& sensory) const
{
    // Look ahead for obstacle: empty above, material below.
    constexpr int CENTER = 4;

    // Check column ahead based on facing direction.
    int check_col = (sensory.facing_x > 0) ? (CENTER + 1) : (CENTER - 1);
    if (check_col < 0 || check_col >= DuckSensoryData::GRID_SIZE) {
        return false;
    }

    // Build obstacle template: empty above, has material below.
    SensoryUtils::SensoryTemplate obstacle_template(1, 2);
    obstacle_template.pattern[0][0] = SensoryUtils::CellPattern(SensoryUtils::MatchMode::IsEmpty);
    obstacle_template.pattern[1][0] = SensoryUtils::CellPattern(SensoryUtils::MatchMode::IsNotEmpty);

    // Check one row above duck's level (rows 2-3, not floor at row 5).
    for (int check_row = CENTER - 2; check_row <= CENTER - 1; ++check_row) {
        if (check_row < 0) {
            continue;
        }
        if (SensoryUtils::matchesTemplate<DuckSensoryData::GRID_SIZE, DuckSensoryData::NUM_MATERIALS>(
                sensory.material_histograms, obstacle_template, check_col, check_row)) {
            return true;
        }
    }

    return false;
}

bool DuckBrain2::isNearMiddle(const DuckSensoryData& sensory) const
{
    // Check if we're at the right distance from center to jump.
    // Goal: apex of jump should be at the center point.
    if (!found_exit_wall_ || entry_wall_x_ < 0) {
        return false;
    }

    int center_x = (entry_wall_x_ + exit_wall_x_) / 2;
    int signed_dist_to_center = sensory.position.x - center_x;

    // Determine if we're approaching the center (not past it).
    bool approaching_center = false;
    if (sensory.velocity.x > 0) {
        // Heading right: must be before or at center.
        approaching_center = (signed_dist_to_center <= 0);
    } else if (sensory.velocity.x < 0) {
        // Heading left: must be at or past center.
        approaching_center = (signed_dist_to_center >= 0);
    }

    if (!approaching_center) {
        return false;
    }

    // Calculate trigger distance based on learned jump distance.
    double trigger_distance;
    if (jump_distance_learned_) {
        // Jump when distance_to_center ≈ half of jump distance (apex at center).
        trigger_distance = learned_jump_distance_ / 2.0;
    } else {
        // No data yet - use conservative narrow zone.
        trigger_distance = 3.0;
    }

    return std::abs(signed_dist_to_center) <= trigger_distance;
}

void DuckBrain2::setRunDirection(Duck& duck, Side target)
{
    if (target == Side::LEFT) {
        current_action_ = DuckAction::RUN_LEFT;
        duck.setWalkDirection(-1.0f);
    }
    else if (target == Side::RIGHT) {
        current_action_ = DuckAction::RUN_RIGHT;
        duck.setWalkDirection(1.0f);
    }
    else {
        current_action_ = DuckAction::WAIT;
        duck.setWalkDirection(0.0f);
    }
}

} // namespace DirtSim
