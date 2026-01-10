#pragma once

namespace DirtSim {

/**
 * Current state of a gamepad's inputs.
 *
 * Updated each frame by GamepadManager::poll().
 */
struct GamepadState {
    float stick_x = 0.0f;      // Left stick horizontal: -1.0 (left) to 1.0 (right).
    float stick_y = 0.0f;      // Left stick vertical: -1.0 (up) to 1.0 (down).
    float dpad_x = 0.0f;       // D-pad horizontal: -1.0, 0.0, or 1.0.
    float dpad_y = 0.0f;       // D-pad vertical: -1.0, 0.0, or 1.0.
    bool button_a = false;     // A/South button (jump).
    bool button_b = false;     // B/East button.
    bool button_x = false;     // X/West button.
    bool button_y = false;     // Y/North button.
    bool button_back = false;  // Back/Select button (reset).
    bool button_start = false; // Start button (spawn duck).
    bool connected = false;    // Whether this gamepad is currently connected.
};

} // namespace DirtSim
