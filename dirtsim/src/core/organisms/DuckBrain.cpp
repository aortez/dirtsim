#include "DuckBrain.h"
#include "Duck.h"
#include "DuckSensoryData.h"
#include "core/LoggingChannels.h"

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

} // namespace DirtSim
