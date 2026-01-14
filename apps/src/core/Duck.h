#pragma once

#include "Entity.h"
#include "Vector2.h"
#include <cstdint>
#include <random>

namespace DirtSim {

class World;

/**
 * @brief Duck entity with physics and AI.
 */
class Duck {
public:
    enum class Action { WAIT, RUN_LEFT, RUN_RIGHT, JUMP };

    Duck(uint32_t entity_id, Vector2<float> spawn_position);

    // Update duck physics and AI.
    void update(World& world, double deltaTime);

    Entity& getEntity() { return entity_; }
    const Entity& getEntity() const { return entity_; }

private:
    Entity entity_;

    // AI state.
    Action current_action_ = Action::WAIT;
    float action_timer_ = 0.0f;
    int run_distance_ = 0;
    float run_start_x_ = 0.0f;
    bool on_ground_ = false;

    std::mt19937 rng_{ std::random_device{}() };

    void updatePhysics(double deltaTime, double gravity);

    void updateAI();

    void pickNextAction();
};

} // namespace DirtSim
