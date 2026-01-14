#include "GamepadManager.h"
#include <spdlog/spdlog.h>

namespace DirtSim {

GamepadManager::GamepadManager()
{
    // Check if SDL gamecontroller subsystem is already initialized.
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        sdl_available_ = true;
        spdlog::debug("[GamepadManager] SDL_INIT_GAMECONTROLLER already initialized.");
    }
    else {
        // Try to initialize it ourselves.
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) {
            we_initialized_sdl_ = true;
            sdl_available_ = true;
            spdlog::info("[GamepadManager] SDL_INIT_GAMECONTROLLER initialized.");
        }
        else {
            spdlog::warn(
                "[GamepadManager] Failed to initialize SDL gamecontroller: {}", SDL_GetError());
            return;
        }
    }

    // Enumerate already-connected controllers.
    int num_joysticks = SDL_NumJoysticks();
    spdlog::debug("[GamepadManager] Found {} joystick(s) at startup.", num_joysticks);

    for (int i = 0; i < num_joysticks; ++i) {
        if (SDL_IsGameController(i)) {
            handleControllerAdded(i);
        }
    }
}

GamepadManager::~GamepadManager()
{
    // Close all controllers.
    for (auto& device : devices_) {
        if (device.controller) {
            SDL_GameControllerClose(device.controller);
            device.controller = nullptr;
        }
    }

    // Only quit the subsystem if we initialized it.
    if (we_initialized_sdl_) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        spdlog::debug("[GamepadManager] SDL_INIT_GAMECONTROLLER shutdown.");
    }
}

void GamepadManager::poll()
{
    if (!sdl_available_) {
        return;
    }

    newly_connected_.clear();
    newly_disconnected_.clear();

    // Process SDL events.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_CONTROLLERDEVICEADDED:
                handleControllerAdded(event.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                handleControllerRemoved(event.cdevice.which);
                break;
            default:
                // Ignore other events.
                break;
        }
    }

    // Update state for each connected controller.
    for (auto& device : devices_) {
        if (device.controller) {
            updateDeviceState(device);
        }
    }
}

size_t GamepadManager::getDeviceCount() const
{
    return devices_.size();
}

GamepadState* GamepadManager::getGamepadState(size_t index)
{
    if (index >= devices_.size()) {
        return nullptr;
    }
    return &devices_[index].state;
}

const GamepadState* GamepadManager::getGamepadState(size_t index) const
{
    if (index >= devices_.size()) {
        return nullptr;
    }
    return &devices_[index].state;
}

std::string GamepadManager::getGamepadName(size_t index) const
{
    if (index >= devices_.size() || !devices_[index].controller) {
        return "";
    }
    const char* name = SDL_GameControllerName(devices_[index].controller);
    return name ? name : "";
}

void GamepadManager::handleControllerAdded(int device_index)
{
    SDL_GameController* controller = SDL_GameControllerOpen(device_index);
    if (!controller) {
        spdlog::warn(
            "[GamepadManager] Failed to open controller {}: {}", device_index, SDL_GetError());
        return;
    }

    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
    SDL_JoystickID joystick_id = SDL_JoystickInstanceID(joystick);
    const char* name = SDL_GameControllerName(controller);

    // Check if we've already opened this controller (by joystick_id).
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (devices_[i].joystick_id == joystick_id) {
            spdlog::debug(
                "[GamepadManager] Controller {} already open in slot {}, skipping duplicate.",
                name ? name : "Unknown",
                i);
            SDL_GameControllerClose(controller);
            return;
        }
    }

    // Find an empty slot or add a new one.
    size_t slot = devices_.size();
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (!devices_[i].controller) {
            slot = i;
            break;
        }
    }

    if (slot == devices_.size()) {
        devices_.emplace_back();
    }

    devices_[slot].controller = controller;
    devices_[slot].joystick_id = joystick_id;
    devices_[slot].state = GamepadState{};
    devices_[slot].state.connected = true;

    newly_connected_.push_back(slot);

    spdlog::info(
        "[GamepadManager] Gamepad {} connected: {} (joystick_id={})",
        slot,
        name ? name : "Unknown",
        joystick_id);
}

void GamepadManager::handleControllerRemoved(SDL_JoystickID joystick_id)
{
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (devices_[i].joystick_id == joystick_id) {
            const char* name = SDL_GameControllerName(devices_[i].controller);
            spdlog::info(
                "[GamepadManager] Gamepad {} disconnected: {}", i, name ? name : "Unknown");

            SDL_GameControllerClose(devices_[i].controller);
            devices_[i].controller = nullptr;
            devices_[i].joystick_id = -1;
            devices_[i].state = GamepadState{};
            devices_[i].state.connected = false;

            newly_disconnected_.push_back(i);
            return;
        }
    }
}

void GamepadManager::updateDeviceState(Device& device)
{
    SDL_GameController* c = device.controller;
    GamepadState& state = device.state;

    // Left stick (normalized to -1.0 to 1.0).
    state.stick_x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    state.stick_y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;

    // D-pad (digital, so -1, 0, or 1).
    bool dpad_left = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    bool dpad_right = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    bool dpad_up = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP);
    bool dpad_down = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN);

    state.dpad_x = (dpad_right ? 1.0f : 0.0f) - (dpad_left ? 1.0f : 0.0f);
    state.dpad_y = (dpad_down ? 1.0f : 0.0f) - (dpad_up ? 1.0f : 0.0f);

    // Buttons.
    state.button_a = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A);
    state.button_b = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B);
    state.button_x = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X);
    state.button_y = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y);
    state.button_back = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK);
    state.button_start = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START);
}

} // namespace DirtSim
