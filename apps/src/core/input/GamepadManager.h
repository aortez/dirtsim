#pragma once

#include "GamepadState.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>

namespace DirtSim {

/**
 * Manages gamepad devices using SDL2 GameController API.
 *
 * Handles initialization, hot-plug detection, and state polling.
 * Owns the SDL_INIT_GAMECONTROLLER subsystem if not already initialized.
 */
class GamepadManager {
public:
    GamepadManager();
    ~GamepadManager();

    // Non-copyable.
    GamepadManager(const GamepadManager&) = delete;
    GamepadManager& operator=(const GamepadManager&) = delete;

    /**
     * Poll SDL events and update all gamepad states.
     * Call this once per frame.
     */
    void poll();

    /**
     * Get the number of gamepad slots (some may be disconnected).
     */
    size_t getDeviceCount() const;

    /**
     * Get the state of a gamepad by index.
     * Returns nullptr if index is out of range.
     * Check state.connected to see if gamepad is actually connected.
     */
    GamepadState* getGamepadState(size_t index);
    const GamepadState* getGamepadState(size_t index) const;

    /**
     * Get the name of a connected gamepad.
     * Returns empty string if not connected.
     */
    std::string getGamepadName(size_t index) const;

    /**
     * Get indices of gamepads that connected since last poll().
     */
    const std::vector<size_t>& getNewlyConnected() const { return newly_connected_; }

    /**
     * Get indices of gamepads that disconnected since last poll().
     */
    const std::vector<size_t>& getNewlyDisconnected() const { return newly_disconnected_; }

    /**
     * Check if SDL gamecontroller subsystem is available.
     */
    bool isAvailable() const { return sdl_available_; }

private:
    struct Device {
        SDL_GameController* controller = nullptr;
        SDL_JoystickID joystick_id = -1;
        GamepadState state;
    };

    void handleControllerAdded(int device_index);
    void handleControllerRemoved(SDL_JoystickID joystick_id);
    void updateDeviceState(Device& device);

    std::vector<Device> devices_;
    std::vector<size_t> newly_connected_;
    std::vector<size_t> newly_disconnected_;
    bool we_initialized_sdl_ = false;
    bool sdl_available_ = false;
};

} // namespace DirtSim
