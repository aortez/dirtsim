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
        duck.setWalkDirection(0.0f);
        return;
    }

    const auto& input = gamepad_input_.value();

    // Movement: combine d-pad and stick, d-pad takes priority.
    float horizontal = input.dpad_x;
    if (horizontal == 0.0f) {
        // Use stick if d-pad is neutral.
        if (std::abs(input.stick_x) > STICK_DEADZONE) {
            horizontal = input.stick_x;
        }
    }

    // Apply movement direction.
    if (horizontal < -STICK_DEADZONE) {
        current_action_ = DuckAction::RUN_LEFT;
        duck.setWalkDirection(-1.0f);
    }
    else if (horizontal > STICK_DEADZONE) {
        current_action_ = DuckAction::RUN_RIGHT;
        duck.setWalkDirection(1.0f);
    }
    else {
        current_action_ = DuckAction::WAIT;
        duck.setWalkDirection(0.0f);
    }

    // Jump: A button, edge-detected, only when on ground.
    bool jump_pressed = input.button_a;
    if (jump_pressed && !last_jump_pressed_ && sensory.on_ground) {
        current_action_ = DuckAction::JUMP;
        duck.jump();
        LOG_DEBUG(Brain, "PlayerDuck {}: JUMP at ({}, {})",
            duck.getId(), sensory.position.x, sensory.position.y);
    }
    last_jump_pressed_ = jump_pressed;

    // Clear input after consuming (brain receives fresh input each tick).
    gamepad_input_.reset();
}

} // namespace DirtSim
