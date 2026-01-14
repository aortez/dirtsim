#include "DuckBrain.h"
#include "Duck.h"
#include "DuckSensoryData.h"
#include "core/LoggingChannels.h"
#include "core/ReflectSerializer.h"
#include "core/organisms/OrganismSensoryData.h"
#include <cmath>

namespace DirtSim {

void RandomDuckBrain::think(Duck& duck, const DuckSensoryData& sensory, double deltaTime)
{
    // Decrement action timer.
    action_timer_ -= static_cast<float>(deltaTime);

    // Check if current action is complete and pick next action if so.
    switch (current_action_) {
        case DuckAction::WAIT:
            if (action_timer_ <= 0.0f) {
                pickNextAction(duck, sensory);
            }
            break;

        case DuckAction::RUN_LEFT:
        case DuckAction::RUN_RIGHT: {
            float current_x = static_cast<float>(sensory.position.x);
            float distance_traveled = std::abs(current_x - run_start_x_);
            if (distance_traveled >= run_target_cells_ || action_timer_ <= 0.0f) {
                pickNextAction(duck, sensory);
            }
            break;
        }

        case DuckAction::JUMP:
            // Jump action completes immediately after initiating.
            pickNextAction(duck, sensory);
            break;
    }

    // Build input from current action state.
    bool should_jump = false;
    float move_x = 0.0f;

    switch (current_action_) {
        case DuckAction::WAIT:
            break;
        case DuckAction::RUN_LEFT:
            move_x = -1.0f;
            break;
        case DuckAction::RUN_RIGHT:
            move_x = 1.0f;
            break;
        case DuckAction::JUMP:
            should_jump = true;
            break;
    }

    // Apply input once at the end.
    duck.setInput({ .move = { move_x, 0.0f }, .jump = should_jump });
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
        LOG_INFO(
            Brain,
            "Duck {}: WAIT for {:.1f}s at ({}, {}).",
            duck.getId(),
            action_timer_,
            sensory.position.x,
            sensory.position.y);
    }
    else if (roll < 7) {
        // 30% chance: Run left.
        current_action_ = DuckAction::RUN_LEFT;
        std::uniform_int_distribution<int> dist_dist(1, 5);
        run_target_cells_ = dist_dist(rng_);
        run_start_x_ = static_cast<float>(sensory.position.x);
        action_timer_ = 5.0f;
        LOG_INFO(
            Brain,
            "Duck {}: RUN_LEFT {} cells from ({}, {}).",
            duck.getId(),
            run_target_cells_,
            sensory.position.x,
            sensory.position.y);
    }
    else if (roll < 10) {
        // 30% chance: Run right.
        current_action_ = DuckAction::RUN_RIGHT;
        std::uniform_int_distribution<int> dist_dist(1, 5);
        run_target_cells_ = dist_dist(rng_);
        run_start_x_ = static_cast<float>(sensory.position.x);
        action_timer_ = 5.0f;
        LOG_INFO(
            Brain,
            "Duck {}: RUN_RIGHT {} cells from ({}, {}).",
            duck.getId(),
            run_target_cells_,
            sensory.position.x,
            sensory.position.y);
    }
    else {
        // 10% chance: Jump (only if on ground).
        if (sensory.on_ground) {
            current_action_ = DuckAction::JUMP;
            LOG_INFO(
                Brain,
                "Duck {}: JUMP at ({}, {}).",
                duck.getId(),
                sensory.position.x,
                sensory.position.y);
        }
        else {
            // Can't jump - wait instead.
            current_action_ = DuckAction::WAIT;
            action_timer_ = 0.3f;
            LOG_INFO(
                Brain,
                "Duck {}: Can't jump (not on ground), WAIT at ({}, {}).",
                duck.getId(),
                sensory.position.x,
                sensory.position.y);
        }
    }
}

void WallBouncingBrain::think(Duck& duck, const DuckSensoryData& sensory, double deltaTime)
{
    float dt = static_cast<float>(deltaTime);

    // Build up input state - will be sent once at the end.
    bool should_jump = false;
    float move_x = 0.0f;

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
        LOG_INFO(
            Brain,
            "Duck {}: pos=({},{}), target={}, left_wall={:.2f}, right_wall={:.2f}, touching={}",
            duck.getId(),
            sensory.position.x,
            sensory.position.y,
            (target_wall_ == TargetWall::LEFT) ? "LEFT" : "RIGHT",
            left_wall,
            right_wall,
            touching);
    }

    if (touching) {
        // Wall touched - record timing and switch direction.
        onWallTouch(current_run_time_);
        current_run_time_ = 0.0f;

        target_wall_ = (target_wall_ == TargetWall::LEFT) ? TargetWall::RIGHT : TargetWall::LEFT;
        LOG_INFO(
            Brain,
            "Duck {}: Wall touched at ({},{}), switching to {} wall (run_time={:.2f}s, "
            "avg={:.2f}s)",
            duck.getId(),
            sensory.position.x,
            sensory.position.y,
            (target_wall_ == TargetWall::LEFT) ? "LEFT" : "RIGHT",
            current_run_time_,
            average_run_time_);
    }

    // Check if jump timer has expired.
    if (enable_jumping_) {
        should_jump = shouldJump(dt);
    }

    // Set movement toward target wall.
    if (target_wall_ == TargetWall::LEFT) {
        current_action_ = DuckAction::RUN_LEFT;
        move_x = -1.0f;
    }
    else {
        current_action_ = DuckAction::RUN_RIGHT;
        move_x = 1.0f;
    }

    // Apply accumulated input state once at the end.
    duck.setInput({ .move = { move_x, 0.0f }, .jump = should_jump });
}

void WallBouncingBrain::pickFurthestWall(const DuckSensoryData& sensory)
{
    // TODO: Determine actual world width from sensory data to pick furthest wall.
    // For now, just start by going right.
    target_wall_ = TargetWall::RIGHT;
    LOG_INFO(
        Brain,
        "Duck {}: Starting WallBouncingBrain - targeting RIGHT wall (pos {})",
        0,
        sensory.position.x);
}

bool WallBouncingBrain::isTouchingWall(const DuckSensoryData& sensory, TargetWall wall) const
{
    // Check material histogram for WALL in adjacent cell.
    // Sensory grid is 9x9, center is at (4, 4).
    // Material::EnumType::Wall = 7.
    // Note: Out-of-bounds cells are marked as WALL by the sensory system.
    constexpr int CENTER = 4;
    constexpr int WALL_MATERIAL_INDEX = 7;
    constexpr double WALL_THRESHOLD = 0.5; // Consider wall present if >= 50% fill.

    if (wall == TargetWall::LEFT) {
        // Check left neighbor (grid position [4][3]).
        double wall_fill = sensory.material_histograms[CENTER][CENTER - 1][WALL_MATERIAL_INDEX];
        return wall_fill >= WALL_THRESHOLD;
    }
    else {
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
    }
    else {
        // Exponential moving average: 20% weight to new sample.
        average_run_time_ = EMA_ALPHA * run_time + (1.0f - EMA_ALPHA) * average_run_time_;
    }

    // Check if this run was consistent (within 20% of average).
    if (enable_jumping_ && average_run_time_ > 0.0f) {
        float deviation = std::abs(run_time - average_run_time_) / average_run_time_;
        if (deviation <= 0.20f) {
            // Consistent timing - schedule jump at midpoint.
            jump_timer_ = average_run_time_ / 2.0f;
            LOG_INFO(
                Brain,
                "Duck: Consistent run time {:.2f}s (avg {:.2f}s, dev {:.1f}%), scheduling jump at "
                "{:.2f}s",
                run_time,
                average_run_time_,
                deviation * 100.0f,
                jump_timer_);
        }
        else {
            LOG_INFO(
                Brain,
                "Duck: Inconsistent run time {:.2f}s (avg {:.2f}s, dev {:.1f}%), no jump scheduled",
                run_time,
                average_run_time_,
                deviation * 100.0f);
        }
    }
}

bool WallBouncingBrain::shouldJump(float deltaTime)
{
    if (jump_timer_ > 0.0f) {
        jump_timer_ -= deltaTime;

        // Jump when timer expires.
        if (jump_timer_ <= 0.0f) {
            current_action_ = DuckAction::JUMP;
            LOG_INFO(Brain, "WallBouncingBrain: Midpoint jump triggered.");
            jump_timer_ = -1.0f;
            return true;
        }
    }
    return false;
}

// ============================================================================
// DuckBrain2 - Dead reckoning with wall-bouncing and exit-seeking behavior
// ============================================================================

void DuckBrain2::think(Duck& duck, const DuckSensoryData& sensory, double deltaTime)
{
    float dt = static_cast<float>(deltaTime);
    elapsed_time_seconds_ += sensory.delta_time_seconds;

    // Initialize on first tick.
    if (!initialized_) {
        initialize(sensory);
    }

    // Update learning systems.
    updateSpeedLearning(sensory);
    updateJumpDistanceLearning(sensory);

    // Update jump cooldown.
    jump_cooldown_seconds_ -= dt;

    // Assess current situation.
    DuckSituation situation = assessSituation(sensory);

    // Debug: log situation and sensory data every frame.
    LOG_DEBUG(
        Brain, "Duck {}: situation={}", duck.getId(), ReflectSerializer::to_json(situation).dump());
    LOG_DEBUG(
        Brain,
        "Duck {}: pos=({},{}), vel=({:.1f},{:.1f}), facing_x={:.1f}, on_ground={}, cooldown={:.2f}",
        duck.getId(),
        sensory.position.x,
        sensory.position.y,
        sensory.velocity.x,
        sensory.velocity.y,
        sensory.facing_x,
        sensory.on_ground,
        jump_cooldown_seconds_);

    // Determine exit side from knowledge.
    Side exit_side = knowledge_.exitSide();

    // Build up input state - will be sent once at the end.
    bool should_jump = false;
    float move_x = 0.0f;

    // Priority: Check if exit door is visible - override everything.
    if (knowledge_.knowsExitWall() && phase_ == Phase::BOUNCING) {
        if (situation.gap_in_exit_wall) {
            LOG_INFO(
                Brain,
                "Duck {}: Detected gap in exit wall at pos={}, exit_wall_x={}. Switching to "
                "EXITING phase.",
                duck.getId(),
                sensory.position.x,
                *knowledge_.exit_wall_x);
            phase_ = Phase::EXITING;
        }
    }

    // Priority: Jump over hazards (cliffs, obstacles) - works in any phase.
    if (situation.on_ground && jump_cooldown_seconds_ <= 0.0f) {
        bool fast_enough = !knowledge_.knowsMaxSpeed()
            || (situation.current_speed >= *knowledge_.max_speed * MIN_SPEED_RATIO_FOR_CLIFF_JUMP);

        // Cliff: jump immediately when detected.
        if (situation.cliff_ahead && fast_enough) {
            should_jump = true;
            jump_start_x_ = sensory.position.x;
            jump_cooldown_seconds_ = JUMP_COOLDOWN;
            current_action_ = DuckAction::JUMP;
            LOG_INFO(
                Brain,
                "Duck {}: Cliff ahead, jumping at speed={:.1f}, pos={}.",
                duck.getId(),
                situation.current_speed,
                sensory.position.x);
        }
        // Obstacle: time the jump to land on top of it.
        else if (situation.obstacle_ahead() && fast_enough) {
            // Calculate optimal jump distance to land on the obstacle.
            // We want to jump so we travel approximately obstacle_distance cells.
            int trigger_distance;
            if (knowledge_.knowsJumpDistance()) {
                // Jump when obstacle is at our learned jump distance.
                trigger_distance = static_cast<int>(*knowledge_.jump_distance + 0.5);
            }
            else {
                // No data yet - use conservative estimate.
                trigger_distance = 3;
            }

            // Jump when obstacle distance matches our jump distance.
            if (situation.obstacle_distance <= trigger_distance) {
                should_jump = true;
                jump_start_x_ = sensory.position.x;
                jump_cooldown_seconds_ = JUMP_COOLDOWN;
                current_action_ = DuckAction::JUMP;
                LOG_INFO(
                    Brain,
                    "Duck {}: Obstacle at {} cells, trigger={}, jumping. speed={:.1f}, pos={}.",
                    duck.getId(),
                    situation.obstacle_distance,
                    trigger_distance,
                    situation.current_speed,
                    sensory.position.x);
            }
        }
    }

    // Phase-specific behavior - determines movement direction.
    switch (phase_) {
        case Phase::SEEKING_EXIT_WALL: {
            // Run toward exit side until we find the wall boundary pattern.
            move_x = moveForSide(exit_side);
            current_action_ =
                (exit_side == Side::LEFT) ? DuckAction::RUN_LEFT : DuckAction::RUN_RIGHT;

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
                LOG_INFO(
                    Brain,
                    "Duck {}: SEEKING at pos={}, sensory grid:\n{}",
                    duck.getId(),
                    sensory.position.x,
                    grid_str);
            }

            // Build wall boundary template: vertical wall with floor.
            SensoryUtils::SensoryTemplate wall_template(2, 4);
            int wall_col = (exit_side == Side::RIGHT) ? 1 : 0;
            int empty_col = (exit_side == Side::RIGHT) ? 0 : 1;

            // Rows 0-2: vertical wall + empty.
            for (int row = 0; row < 3; ++row) {
                wall_template.pattern[row][wall_col] = SensoryUtils::CellPattern(
                    SensoryUtils::MatchMode::Is, { Material::EnumType::Wall });
                wall_template.pattern[row][empty_col] =
                    SensoryUtils::CellPattern(SensoryUtils::MatchMode::IsEmpty);
            }
            // Row 3: floor (both columns are wall).
            wall_template.pattern[3][0] = SensoryUtils::CellPattern(
                SensoryUtils::MatchMode::Is, { Material::EnumType::Wall });
            wall_template.pattern[3][1] = SensoryUtils::CellPattern(
                SensoryUtils::MatchMode::Is, { Material::EnumType::Wall });

            // Find wall boundary pattern.
            auto match = SensoryUtils::
                findTemplate<DuckSensoryData::GRID_SIZE, DuckSensoryData::NUM_MATERIALS>(
                    sensory.material_histograms, wall_template);

            if (match.found) {
                // Calculate actual wall position from grid match.
                int wall_grid_col = match.col + wall_col;
                knowledge_.exit_wall_x = sensory.world_offset.x + wall_grid_col;
                phase_ = Phase::BOUNCING;
                current_target_ = knowledge_.spawn_side;
                jump_cooldown_seconds_ = JUMP_COOLDOWN;
                LOG_INFO(
                    Brain,
                    "Duck {}: Found exit wall boundary at grid col {}, world x={}. Starting "
                    "BOUNCING phase.",
                    duck.getId(),
                    wall_grid_col,
                    *knowledge_.exit_wall_x);
            }
            break;
        }

        case Phase::BOUNCING: {
            // Bounce between walls, jumping when appropriate.
            move_x = moveForSide(current_target_);
            current_action_ =
                (current_target_ == Side::LEFT) ? DuckAction::RUN_LEFT : DuckAction::RUN_RIGHT;

            // Check if we hit a wall and need to turn around.
            bool touching_entry_wall = isTouchingWall(sensory, knowledge_.spawn_side);
            bool touching_exit_wall = isTouchingWall(sensory, exit_side);

            if (current_target_ == knowledge_.spawn_side && touching_entry_wall) {
                current_target_ = exit_side;
                jump_cooldown_seconds_ = 0.0f;
                LOG_INFO(Brain, "Duck {}: Hit entry wall, bouncing toward exit.", duck.getId());
            }
            else if (current_target_ == exit_side && touching_exit_wall) {
                current_target_ = knowledge_.spawn_side;
                jump_cooldown_seconds_ = 0.0f;
                LOG_INFO(Brain, "Duck {}: Hit exit wall, bouncing toward entry.", duck.getId());
            }

            // Jump decision: middle jumps for fun (hazard jumps handled in priority section).
            if (situation.on_ground && jump_cooldown_seconds_ <= 0.0f && situation.at_full_speed) {
                if (situation.near_middle) {
                    should_jump = true;
                    jump_start_x_ = sensory.position.x;
                    jump_cooldown_seconds_ = JUMP_COOLDOWN;
                    current_action_ = DuckAction::JUMP;
                    double max_spd = knowledge_.max_speed.value_or(0.0);
                    LOG_INFO(
                        Brain,
                        "Duck {}: Jumping in middle. speed={:.1f}, max_speed={:.1f}, pos={}.",
                        duck.getId(),
                        situation.current_speed,
                        max_spd,
                        sensory.position.x);
                }
            }
            break;
        }

        case Phase::EXITING:
            // Run toward exit as fast as possible.
            move_x = moveForSide(exit_side);
            current_action_ =
                (exit_side == Side::LEFT) ? DuckAction::RUN_LEFT : DuckAction::RUN_RIGHT;
            break;
    }

    // Apply accumulated input state once at the end.
    duck.setInput({ .move = { move_x, 0.0f }, .jump = should_jump });

    // Debug logging every 60 frames.
    if (debug_frame_counter_++ % 60 == 0) {
        const char* phase_str = (phase_ == Phase::SEEKING_EXIT_WALL) ? "SEEKING"
            : (phase_ == Phase::BOUNCING)                            ? "BOUNCING"
                                                                     : "EXITING";
        const char* spawn_str = (knowledge_.spawn_side == Side::LEFT) ? "L" : "R";
        int exit_x = knowledge_.exit_wall_x.value_or(-1);
        LOG_INFO(
            Brain,
            "Duck {}: phase={}, spawn={}, exit_wall_x={}, pos={}, vel={:.1f}, elapsed={:.1f}s",
            duck.getId(),
            phase_str,
            spawn_str,
            exit_x,
            sensory.position.x,
            sensory.velocity.x,
            elapsed_time_seconds_);
    }
}

void DuckBrain2::initialize(const DuckSensoryData& sensory)
{
    knowledge_.spawn_x = sensory.position.x;
    knowledge_.spawn_side = detectSpawnSide(sensory);
    knowledge_.entry_wall_x = sensory.position.x; // We're next to entry wall at spawn.
    current_target_ = knowledge_.exitSide();
    initialized_ = true;

    LOG_INFO(
        Brain,
        "DuckBrain2: Initialized at x={}, spawn_side={}, heading {}",
        knowledge_.spawn_x,
        knowledge_.spawn_side == Side::LEFT ? "LEFT" : "RIGHT",
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
            right_wall_total +=
                sensory.material_histograms[y][DuckSensoryData::GRID_SIZE - 1][WALL_MATERIAL_INDEX];
        }
    }

    LOG_INFO(
        Brain,
        "DuckBrain2: Spawn detection - left_wall={:.2f}, right_wall={:.2f}",
        left_wall_total,
        right_wall_total);

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
    if (!knowledge_.knowsExitWall()) {
        return false;
    }

    // Calculate where the exit wall appears in our current sensory grid.
    int exit_wall_grid_x = *knowledge_.exit_wall_x - sensory.world_offset.x;

    // Exit wall must be visible in our sensory grid.
    if (exit_wall_grid_x < 0 || exit_wall_grid_x >= DuckSensoryData::GRID_SIZE) {
        return false;
    }

    // Build door template: empty opening above floor.
    SensoryUtils::SensoryTemplate door_template(1, 2);
    door_template.pattern[0][0] = SensoryUtils::CellPattern(SensoryUtils::MatchMode::IsEmpty);
    door_template.pattern[1][0] =
        SensoryUtils::CellPattern(SensoryUtils::MatchMode::Is, { Material::EnumType::Wall });

    // Check around center row for door pattern at the recorded wall position.
    constexpr int CENTER_Y = 4;
    for (int check_y = CENTER_Y - 1; check_y <= CENTER_Y + 1; ++check_y) {
        if (SensoryUtils::
                matchesTemplate<DuckSensoryData::GRID_SIZE, DuckSensoryData::NUM_MATERIALS>(
                    sensory.material_histograms, door_template, exit_wall_grid_x, check_y)) {
            return true;
        }
    }

    return false;
}

int DuckBrain2::findObstacleDistance(const DuckSensoryData& sensory) const
{
    // Scan ahead for floor obstacles (hurdles) that block the duck's path.
    // These are solid materials at duck's level with empty space above.
    //
    // Returns distance to nearest obstacle (1-4 cells), or -1 if none found.
    constexpr int CENTER = 4;
    constexpr int FLOOR_ROW = CENTER + 1; // Row below duck.

    int direction = (sensory.facing_x > 0) ? 1 : -1;

    auto hasMaterialAt = [&](int row, int col) -> bool {
        if (row < 0 || row >= DuckSensoryData::GRID_SIZE) {
            return false;
        }
        if (col < 0 || col >= DuckSensoryData::GRID_SIZE) {
            return false;
        }
        double total_fill = 0.0;
        for (int mat = 0; mat < DuckSensoryData::NUM_MATERIALS; ++mat) {
            if (mat != static_cast<int>(Material::EnumType::Air)) {
                total_fill += sensory.material_histograms[row][col][mat];
            }
        }
        return total_fill >= 0.3;
    };

    // Scan from nearest to farthest (1-4 cells ahead).
    for (int distance = 1; distance <= 4; ++distance) {
        int check_col = CENTER + (direction * distance);
        if (check_col < 0 || check_col >= DuckSensoryData::GRID_SIZE) {
            continue;
        }

        bool has_obstacle_at_level = hasMaterialAt(CENTER, check_col);
        bool has_empty_above = !hasMaterialAt(CENTER - 1, check_col);
        bool has_floor_below = hasMaterialAt(FLOOR_ROW, check_col);

        // Obstacle detected: something at duck's level, with floor below,
        // and empty above (so it's jumpable, not a wall).
        if (has_obstacle_at_level && has_floor_below && has_empty_above) {
            return distance;
        }
    }

    return -1; // No obstacle found.
}

bool DuckBrain2::isNearMiddle(const DuckSensoryData& sensory) const
{
    // Check if we're at the right distance from center to jump.
    // Goal: apex of jump should be at the center point.
    if (!knowledge_.knowsExitWall() || knowledge_.entry_wall_x < 0) {
        return false;
    }

    int center_x = (knowledge_.entry_wall_x + *knowledge_.exit_wall_x) / 2;
    int signed_dist_to_center = sensory.position.x - center_x;

    // Determine if we're approaching the center (not past it).
    bool approaching_center = false;
    if (sensory.velocity.x > 0) {
        // Heading right: must be before or at center.
        approaching_center = (signed_dist_to_center <= 0);
    }
    else if (sensory.velocity.x < 0) {
        // Heading left: must be at or past center.
        approaching_center = (signed_dist_to_center >= 0);
    }

    if (!approaching_center) {
        return false;
    }

    // Calculate trigger distance based on learned jump distance.
    double trigger_distance;
    if (knowledge_.knowsJumpDistance()) {
        // Jump when distance_to_center ≈ half of jump distance (apex at center).
        trigger_distance = *knowledge_.jump_distance / 2.0;
    }
    else {
        // No data yet - use conservative narrow zone.
        trigger_distance = 3.0;
    }

    return std::abs(signed_dist_to_center) <= trigger_distance;
}

float DuckBrain2::moveForSide(Side side) const
{
    if (side == Side::LEFT) {
        return -1.0f;
    }
    else if (side == Side::RIGHT) {
        return 1.0f;
    }
    return 0.0f;
}

DuckSituation DuckBrain2::assessSituation(const DuckSensoryData& sensory) const
{
    DuckSituation situation;

    // Physical state.
    situation.on_ground = sensory.on_ground;
    situation.current_speed = std::abs(sensory.velocity.x);
    situation.facing_direction = (sensory.velocity.x > 0.1) ? 1
        : (sensory.velocity.x < -0.1)                       ? -1
                                                            : 0;

    // Spatial awareness.
    situation.wall_ahead =
        isTouchingWall(sensory, (situation.facing_direction > 0) ? Side::RIGHT : Side::LEFT);
    situation.obstacle_distance = findObstacleDistance(sensory);
    situation.cliff_ahead = detectsCliffAhead(sensory);
    situation.gap_in_exit_wall = detectsGapInExitWall(sensory);

    // Derived assessments (require knowledge).
    situation.near_middle = isNearMiddle(sensory);

    if (knowledge_.knowsMaxSpeed() && *knowledge_.max_speed > 0) {
        situation.at_full_speed =
            (situation.current_speed >= *knowledge_.max_speed * MIN_SPEED_RATIO_FOR_JUMP);
    }
    else {
        // Fallback: use absolute threshold.
        situation.at_full_speed = (situation.current_speed >= MIN_SPEED_FOR_JUMP);
    }

    // Can we clear a cliff if we jump now?
    // For now, assume we can if we know our jump distance and it's > 2 cells.
    // TODO: Measure actual cliff width and compare.
    if (knowledge_.knowsJumpDistance()) {
        situation.can_clear_cliff = (*knowledge_.jump_distance >= 2.0);
    }

    return situation;
}

void DuckBrain2::updateSpeedLearning(const DuckSensoryData& sensory)
{
    if (knowledge_.knowsMaxSpeed()) {
        return; // Already learned.
    }

    double current_speed = std::abs(sensory.velocity.x);

    // Check if speed is steady (within margin of last speed).
    if (std::abs(current_speed - last_speed_) < SPEED_CONVERGENCE_MARGIN) {
        steady_speed_time_ += sensory.delta_time_seconds;

        // Lock in max speed after 1 second of steady state.
        if (steady_speed_time_ >= SPEED_CONVERGENCE_TIME) {
            knowledge_.max_speed = current_speed;
            LOG_INFO(
                Brain,
                "Duck: Learned max speed = {:.1f} cells/sec (converged for {:.1f}s)",
                current_speed,
                steady_speed_time_);
        }
    }
    else {
        // Speed changed - reset steady state timer.
        steady_speed_time_ = 0.0;
    }

    last_speed_ = current_speed;
}

void DuckBrain2::updateJumpDistanceLearning(const DuckSensoryData& sensory)
{
    // Track landing after a jump we initiated (jump_start_x_ is set when we call duck.jump()).
    bool was_in_air = in_jump_;
    in_jump_ = !sensory.on_ground;

    // Just landed after a jump we initiated.
    if (was_in_air && !in_jump_ && jump_start_x_ >= 0) {
        int jump_distance = std::abs(sensory.position.x - jump_start_x_);

        // Update learned jump distance with exponential moving average.
        if (!knowledge_.knowsJumpDistance()) {
            knowledge_.jump_distance = static_cast<double>(jump_distance);
            LOG_INFO(Brain, "Duck: First jump distance = {} cells", jump_distance);
        }
        else {
            knowledge_.jump_distance = JUMP_DISTANCE_EMA_ALPHA * jump_distance
                + (1.0f - JUMP_DISTANCE_EMA_ALPHA) * (*knowledge_.jump_distance);
            LOG_INFO(
                Brain,
                "Duck: Jump distance = {} cells, EMA = {:.1f} cells",
                jump_distance,
                *knowledge_.jump_distance);
        }

        jump_start_x_ = -1;
    }
}

bool DuckBrain2::detectsCliffAhead(const DuckSensoryData& sensory) const
{
    // Detect a cliff: we have floor under us, but floor drops off ahead.
    //
    // Sensory grid layout (9x9, CENTER at row 4, col 4):
    //   Row 4 = duck's level
    //   Row 5 = floor level (one below duck)
    //
    // Cliff pattern: floor exists at current position, absent ahead.
    constexpr int CENTER = 4;
    constexpr int FLOOR_ROW = CENTER + 1; // Row below duck.

    // Determine which column to check based on facing direction.
    int check_col = (sensory.facing_x > 0) ? (CENTER + 1) : (CENTER - 1);
    if (check_col < 0 || check_col >= DuckSensoryData::GRID_SIZE) {
        return false;
    }

    // Must be on ground to care about cliffs.
    if (!sensory.on_ground) {
        return false;
    }

    // Check if the floor drops off ahead.
    // We look for: current position has floor, position ahead does not.
    auto hasFloorAt = [&](int col) -> bool {
        if (col < 0 || col >= DuckSensoryData::GRID_SIZE) {
            return false;
        }
        if (FLOOR_ROW >= DuckSensoryData::GRID_SIZE) {
            return false;
        }
        // Check if any solid material is present (not AIR).
        // Sum all non-AIR materials.
        double total_fill = 0.0;
        for (int mat = 0; mat < DuckSensoryData::NUM_MATERIALS; ++mat) {
            if (mat != static_cast<int>(Material::EnumType::Air)) {
                total_fill += sensory.material_histograms[FLOOR_ROW][col][mat];
            }
        }
        return total_fill >= 0.3; // Threshold for "has floor."
    };

    bool has_floor_here = hasFloorAt(CENTER);
    bool has_floor_ahead = hasFloorAt(check_col);

    // Also check 2 cells ahead for a wider cliff.
    int check_col_2 = (sensory.facing_x > 0) ? (CENTER + 2) : (CENTER - 2);
    bool has_floor_ahead_2 = hasFloorAt(check_col_2);

    // Cliff detected if we have floor but ahead is empty.
    if (has_floor_here && !has_floor_ahead && !has_floor_ahead_2) {
        return true;
    }

    return false;
}

} // namespace DirtSim
