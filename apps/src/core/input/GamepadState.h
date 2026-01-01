#pragma once

namespace DirtSim {

/**
 * Current state of a gamepad's inputs.
 *
 * Updated each frame by GamepadManager::poll().
 */
struct GamepadState {
    float stick_x = 0.0f;   // Left stick horizontal: -1.0 (left) to 1.0 (right).
    float stick_y = 0.0f;   // Left stick vertical: -1.0 (up) to 1.0 (down).
    float dpad_x = 0.0f;    // D-pad horizontal: -1.0, 0.0, or 1.0.
    float dpad_y = 0.0f;    // D-pad vertical: -1.0, 0.0, or 1.0.
    bool button_a = false;  // A/South button (spawn duck if none, then jump).
    bool button_b = false;  // B/East button (spawn duck if none, future use otherwise).
    bool connected = false; // Whether this gamepad is currently connected.
};

} // namespace DirtSim
