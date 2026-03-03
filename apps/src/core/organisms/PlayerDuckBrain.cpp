#include "PlayerDuckBrain.h"
#include "Duck.h"
#include "core/LoggingChannels.h"

namespace DirtSim {

void PlayerDuckBrain::setGamepadInput(const GamepadState& state)
{
    gamepad_input_ = state;
}

void PlayerDuckBrain::think(Duck& duck, const DuckSensoryData& sensory, double /*deltaTime*/)
{
    // No input yet - just wait.
    if (!gamepad_input_.has_value()) {
        current_action_ = DuckAction::WAIT;
        duck.setInput({ .move = {}, .jump = false });
        return;
    }

    const auto& input = gamepad_input_.value();

    // Determine horizontal direction (d-pad takes priority over stick).
    float horizontal = input.dpad_x;
    if (horizontal == 0.0f && std::abs(input.stick_x) > STICK_DEADZONE) {
        horizontal = input.stick_x;
    }

    // Determine vertical direction (d-pad takes priority over stick).
    // Gamepad Y is negative-up, duck move.y is positive-up, so negate.
    float vertical = -input.dpad_y;
    if (vertical == 0.0f && std::abs(input.stick_y) > STICK_DEADZONE) {
        vertical = -input.stick_y;
    }

    // Walk (60%) or run (100%) based on B button.
    const bool run = input.button_b;
    const float speed_scale = run ? 1.0f : 0.6f;

    float move_x = 0.0f;
    if (std::abs(horizontal) > STICK_DEADZONE) {
        move_x = horizontal * speed_scale;
        current_action_ = horizontal < 0.0f ? DuckAction::RUN_LEFT : DuckAction::RUN_RIGHT;
    }
    else {
        current_action_ = DuckAction::WAIT;
    }

    // Track press edges for action/logging while forwarding held state to the duck.
    const bool jump_held = input.button_a;
    const bool jump_pressed = jump_held && !last_jump_pressed_;
    if (jump_pressed && sensory.on_ground) {
        current_action_ = DuckAction::JUMP;
        LOG_DEBUG(
            Brain,
            "PlayerDuck {}: JUMP at ({}, {}).",
            duck.getId(),
            sensory.position.x,
            sensory.position.y);
    }
    last_jump_pressed_ = jump_held;

    // Send combined input matching the NES-style controller model (x, y, A, B).
    duck.setInput({ .move = { move_x, vertical }, .jump = jump_held, .run = run });

    // Clear input after consuming (brain receives fresh input each tick).
    gamepad_input_.reset();
}

} // namespace DirtSim
