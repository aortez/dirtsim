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

    // Determine movement direction (d-pad takes priority over stick).
    float horizontal = input.dpad_x;
    if (horizontal == 0.0f) {
        // Use stick if d-pad is neutral.
        if (std::abs(input.stick_x) > STICK_DEADZONE) {
            horizontal = input.stick_x;
        }
    }

    // Convert to continuous force: walk (60%) or run (100%) based on B button.
    float move_x = 0.0f;
    if (horizontal < -STICK_DEADZONE) {
        move_x = input.button_b ? -1.0f : -0.6f;
        current_action_ = DuckAction::RUN_LEFT;
    }
    else if (horizontal > STICK_DEADZONE) {
        move_x = input.button_b ? 1.0f : 0.6f;
        current_action_ = DuckAction::RUN_RIGHT;
    }
    else {
        current_action_ = DuckAction::WAIT;
    }

    // Jump: A button, edge-detected, only when on ground.
    bool jump_pressed = input.button_a;
    bool should_jump = jump_pressed && !last_jump_pressed_ && sensory.on_ground;
    if (should_jump) {
        current_action_ = DuckAction::JUMP;
        LOG_DEBUG(
            Brain,
            "PlayerDuck {}: JUMP at ({}, {}).",
            duck.getId(),
            sensory.position.x,
            sensory.position.y);
    }
    last_jump_pressed_ = jump_pressed;

    // Send combined input (movement AND jump together).
    duck.setInput({ .move = { move_x, 0.0f }, .jump = should_jump });

    // Clear input after consuming (brain receives fresh input each tick).
    gamepad_input_.reset();
}

} // namespace DirtSim
