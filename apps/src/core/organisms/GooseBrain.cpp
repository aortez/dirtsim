#include "GooseBrain.h"
#include "Goose.h"
#include "core/LoggingChannels.h"
#include <cmath>

namespace DirtSim {

void RandomGooseBrain::think(Goose& goose, const GooseSensoryData& sensory, double deltaTime)
{
    // Decrement action timer.
    action_timer_ -= static_cast<float>(deltaTime);

    // Check if current action is complete.
    bool action_complete = false;

    switch (current_action_) {
        case GooseAction::WAIT:
            action_complete = (action_timer_ <= 0.0f);
            goose.setWalkDirection(0.0f);
            break;

        case GooseAction::RUN_LEFT:
        case GooseAction::RUN_RIGHT: {
            float current_x = static_cast<float>(sensory.position.x);
            float distance_traveled = std::abs(current_x - run_start_x_);

            if (distance_traveled >= run_target_cells_ || action_timer_ <= 0.0f) {
                action_complete = true;
                goose.setWalkDirection(0.0f);
            }
            else {
                float direction = (current_action_ == GooseAction::RUN_LEFT) ? -1.0f : 1.0f;
                goose.setWalkDirection(direction);
            }
            break;
        }

        case GooseAction::JUMP:
            action_complete = true;
            break;
    }

    if (action_complete) {
        pickNextAction(goose, sensory);
    }
}

void RandomGooseBrain::pickNextAction(Goose& goose, const GooseSensoryData& sensory)
{
    std::uniform_int_distribution<int> action_dist(0, 10);
    int roll = action_dist(rng_);

    if (roll < 4) {
        // 40% chance: Wait.
        current_action_ = GooseAction::WAIT;
        std::uniform_real_distribution<float> wait_dist(0.5f, 2.0f);
        action_timer_ = wait_dist(rng_);
        goose.setWalkDirection(0.0f);
        LOG_INFO(
            Brain,
            "Goose {}: WAIT for {:.1f}s at ({}, {})",
            goose.getId(),
            action_timer_,
            sensory.position.x,
            sensory.position.y);
    }
    else if (roll < 7) {
        // 30% chance: Run left.
        current_action_ = GooseAction::RUN_LEFT;
        std::uniform_int_distribution<int> dist_dist(1, 5);
        run_target_cells_ = dist_dist(rng_);
        run_start_x_ = static_cast<float>(sensory.position.x);
        action_timer_ = 5.0f;
        LOG_INFO(
            Brain,
            "Goose {}: RUN_LEFT {} cells from ({}, {})",
            goose.getId(),
            run_target_cells_,
            sensory.position.x,
            sensory.position.y);
    }
    else if (roll < 10) {
        // 30% chance: Run right.
        current_action_ = GooseAction::RUN_RIGHT;
        std::uniform_int_distribution<int> dist_dist(1, 5);
        run_target_cells_ = dist_dist(rng_);
        run_start_x_ = static_cast<float>(sensory.position.x);
        action_timer_ = 5.0f;
        LOG_INFO(
            Brain,
            "Goose {}: RUN_RIGHT {} cells from ({}, {})",
            goose.getId(),
            run_target_cells_,
            sensory.position.x,
            sensory.position.y);
    }
    else {
        // 10% chance: Jump (only if on ground).
        if (sensory.on_ground) {
            current_action_ = GooseAction::JUMP;
            goose.jump();
            LOG_INFO(
                Brain,
                "Goose {}: JUMP at ({}, {})",
                goose.getId(),
                sensory.position.x,
                sensory.position.y);
        }
        else {
            // Can't jump - wait instead.
            current_action_ = GooseAction::WAIT;
            action_timer_ = 0.5f;
            goose.setWalkDirection(0.0f);
        }
    }
}

} // namespace DirtSim
