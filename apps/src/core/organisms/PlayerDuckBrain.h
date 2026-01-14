#pragma once

#include "DuckBrain.h"
#include "core/input/GamepadState.h"
#include <optional>

namespace DirtSim {

/**
 * Player-controlled duck brain that responds to gamepad input.
 *
 * Behavior:
 * - D-pad/stick left → RUN_LEFT
 * - D-pad/stick right → RUN_RIGHT
 * - Neutral → WAIT (stop)
 * - A button (edge-detected, on ground) → JUMP
 *
 * Note: Duck must be spawned first by pressing any button (handled by SimRunning).
 */
class PlayerDuckBrain : public DuckBrain {
public:
    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;
    void setGamepadInput(const GamepadState& state) override;

private:
    std::optional<GamepadState> gamepad_input_;
    bool last_jump_pressed_ = false; // For edge detection on A button.

    // Deadzone for analog stick.
    static constexpr float STICK_DEADZONE = 0.2f;
};

} // namespace DirtSim
