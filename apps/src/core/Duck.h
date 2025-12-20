#pragma once

#include "Entity.h"
#include "Vector2.h"
#include <cstdint>
#include <random>

namespace DirtSim {

class World;

/**
 * @brief Duck entity with physics and AI.
 *
 * The duck has:
 * - Super Mario Bros style physics (acceleration, max speed, drag, jump)
 * - Simple state machine AI (run, jump, wait)
 * - Gravity and collision response (ghost for now - no collision)
 */
class Duck {
public:
    enum class Action {
        WAIT,
        RUN_LEFT,
        RUN_RIGHT,
        JUMP
    };

    Duck(uint32_t entity_id, Vector2<float> spawn_position);

    // Update duck physics and AI.
    void update(World& world, double deltaTime);

    // Get the underlying entity for rendering.
    Entity& getEntity() { return entity_; }
    const Entity& getEntity() const { return entity_; }

private:
    Entity entity_;

    // AI state.
    Action current_action_ = Action::WAIT;
    float action_timer_ = 0.0f;
    int run_distance_ = 0;        // Target cells to run (1-5).
    float run_start_x_ = 0.0f;    // X position when run started.
    bool on_ground_ = false;

    std::mt19937 rng_{ std::random_device{}() };

    // Physics update.
    void updatePhysics(double deltaTime, double gravity);

    // AI update.
    void updateAI();

    // Pick next action.
    void pickNextAction();
};

} // namespace DirtSim
