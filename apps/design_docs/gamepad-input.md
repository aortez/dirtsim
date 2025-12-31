# Gamepad Input for Organism Control

## Summary

Add gamepad support to directly control organisms (like a platformer). When a gamepad connects, spawn a player-controlled Goose that responds to D-pad/stick for movement and A button for jump.

## Requirements

1. **Cross-platform**: Linux desktop (Wayland/X11) and Raspberry Pi (FBDEV).
2. **Direct organism control**: D-pad/stick controls movement, button for jump.
3. **Player-spawned organism**: Gamepad spawns and controls its own dedicated creature.

## Key Design Decisions

1. **Implement in server process** (not UI) - Organisms and brains live server-side, avoids WebSocket latency.
2. **SDL2 GameController API** - See rationale below.
3. **PlayerGooseBrain** - New brain that reads gamepad state instead of AI.
4. **Gamepad spawns its own organism** - When connected, creates a dedicated Goose.
5. **GamepadManager owned by Server::StateMachine** - Clean ownership, easy access from states.
6. **Poll every physics tick (~60Hz)** - Simple, consistent, plenty fast for gamepad input.
7. **Multi-gamepad support from the start** - Each gamepad spawns its own Goose.

## Why SDL2 GameController API

**Considered alternatives:**
- **SDL2 Joystick API** - Lower level, but button numbers vary by controller (Xbox A != PS4 X). More work for same result.
- **Linux evdev directly** - Zero dependencies, but requires custom button mapping and device enumeration. Hot-plug needs libudev or polling `/dev/input/`.
- **Hybrid SDL2 + evdev** - Maximum compatibility but more code to maintain.

**Chose SDL2 GameController because:**
- Already an optional dependency in CMakeLists.txt.
- Automatic button mapping via SDL's gamecontrollerdb (Xbox, PS4, Switch Pro all map to same interface).
- Built-in hot-plug detection via `SDL_CONTROLLERDEVICEADDED/REMOVED` events.
- Handles dead zones and axis normalization.
- Cross-platform (desktop + Pi).

**SDL initialization coordination:**
- SDL might already be initialized by SDL display backend.
- SDL might not be initialized (Wayland/FBDEV backend).
- Use `SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)` - safe to call multiple times.
- Check `SDL_WasInit(SDL_INIT_GAMECONTROLLER)` before cleanup.

**Build-time optional:**
- SDL2 is optional at build time (checked via `pkg_check_modules`).
- If SDL2 not found, `DIRTSIM_HAS_GAMEPAD` is not defined.
- GamepadManager.cpp is only compiled when SDL2 is available.
- CLI `gamepad-test` command shows error message when not available.
- Pi builds (FBDEV) typically don't have SDL2, so gamepad is desktop-only for now.

## Architecture

```
Server Process:
  GamepadManager (polls SDL2)
       |
       v
  PlayerGooseBrain (receives gamepad state)
       |
       v
  Goose (organism with PlayerGooseBrain)
       |
       v
  OrganismManager.update() -> World.advanceTime()
```

## Implementation Phases

### Phase 1: Gamepad Abstraction Layer

**New files in `src/core/input/`:**

1. `GamepadState.h` - Simple POD struct:
   ```cpp
   struct GamepadState {
       float stick_x = 0.0f;   // -1.0 to 1.0 (left stick horizontal).
       float stick_y = 0.0f;   // -1.0 to 1.0 (left stick vertical).
       float dpad_x = 0.0f;    // -1.0, 0.0, or 1.0.
       float dpad_y = 0.0f;    // -1.0, 0.0, or 1.0.
       bool button_a = false;  // Jump (SDL_CONTROLLER_BUTTON_A / South).
       bool button_b = false;  // Future use (SDL_CONTROLLER_BUTTON_B / East).
       bool connected = false;
   };
   ```

2. `GamepadManager.h/.cpp` - Owns SDL subsystem and all devices:
   ```cpp
   class GamepadManager {
   public:
       GamepadManager();   // Calls SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER).
       ~GamepadManager();  // Closes controllers, calls SDL_QuitSubSystem if we initialized.

       void poll();  // Pumps SDL events, updates all device states.

       size_t getDeviceCount() const;
       GamepadState* getGamepadState(int index);  // nullptr if not connected.

       // Returns indices of gamepads that connected/disconnected since last poll.
       std::vector<int> getNewlyConnected();
       std::vector<int> getNewlyDisconnected();

   private:
       struct Device {
           SDL_GameController* controller = nullptr;
           SDL_JoystickID joystick_id = -1;
           GamepadState state;
       };
       std::vector<Device> devices_;
       std::vector<int> newly_connected_;
       std::vector<int> newly_disconnected_;
       bool we_initialized_sdl_ = false;
   };
   ```

**SDL event handling in `poll()`:**
```cpp
void GamepadManager::poll() {
    newly_connected_.clear();
    newly_disconnected_.clear();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_CONTROLLERDEVICEADDED:
                // Open controller, add to devices_, record in newly_connected_.
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                // Find by joystick_id, close, mark disconnected, record in newly_disconnected_.
                break;
        }
    }

    // Update state for each connected controller.
    for (auto& device : devices_) {
        if (device.controller) {
            device.state.stick_x = SDL_GameControllerGetAxis(device.controller,
                SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            // ... etc.
        }
    }
}
```

### Phase 2: PlayerGooseBrain

**New file: `src/core/organisms/PlayerGooseBrain.h/.cpp`**

```cpp
class PlayerGooseBrain : public GooseBrain {
public:
    explicit PlayerGooseBrain(const GamepadState* state);
    void think(Goose& goose, const GooseSensoryData& sensory, double deltaTime) override;
private:
    const GamepadState* gamepad_state_;
    bool last_jump_pressed_ = false;  // Edge detection.
};
```

Implementation:
- Read `dpad_x` or `stick_x` for movement direction.
- Call `goose.setWalkDirection(move)`.
- On A button press (edge-detected), call `goose.jump()`.

### Phase 3: Server Integration

**Modify: `src/server/Event.h`**
- Add `GamepadConnectedEvent { int gamepad_index; }`
- Add `GamepadDisconnectedEvent { int gamepad_index; }`
- Add to Event::Variant.

**Modify: `src/server/states/SimRunning.h/.cpp`**
- Add `std::map<int, OrganismId> gamepad_organisms_`.
- Handle `GamepadConnectedEvent`: Create Goose with `PlayerGooseBrain`.
- Handle `GamepadDisconnectedEvent`: Optionally remove organism.

**Modify: `src/server/StateMachine.h/.cpp`**
- Add `std::unique_ptr<GamepadManager> gamepadManager_`.
- Initialize in constructor.
- Add `pollGamepads()` method.

**Modify: `src/server/main.cpp`**
- Call `stateMachine.pollGamepads()` in main loop (after event processing).

### Phase 4: Build System

**Modify: `CMakeLists.txt`**
- Add new source files to `dirtsim-server-lib`.
- SDL2 already conditionally linked via `LV_USE_SDL`.
- Add compile definition for gamepad support.

## Files to Create

| File | Purpose |
|------|---------|
| `src/core/input/GamepadState.h` | Gamepad state struct. |
| `src/core/input/GamepadManager.h` | Manager header (owns SDL subsystem + devices). |
| `src/core/input/GamepadManager.cpp` | Manager implementation. |
| `src/core/organisms/PlayerGooseBrain.h` | Player brain header. |
| `src/core/organisms/PlayerGooseBrain.cpp` | Player brain implementation. |

## Files to Modify

| File | Changes |
|------|---------|
| `src/server/Event.h` | Add GamepadConnected/Disconnected events. |
| `src/server/states/SimRunning.h` | Add gamepad→organism mapping. |
| `src/server/states/SimRunning.cpp` | Handle gamepad events, spawn player goose. |
| `src/server/StateMachine.h` | Add GamepadManager member. |
| `src/server/StateMachine.cpp` | Initialize GamepadManager. |
| `src/server/main.cpp` | Poll gamepads in main loop. |
| `src/cli/main.cpp` | Add `gamepad-test` command. |
| `CMakeLists.txt` | Add new source files, link SDL2 to CLI. |

## Control Mapping

| Input | Action |
|-------|--------|
| D-pad Left / Left Stick Left | Walk left. |
| D-pad Right / Left Stick Right | Walk right. |
| A Button (South) | Jump. |

## Known Issues

**Duplicate gamepad reporting**: On some systems, a single controller may appear as two devices (e.g., Gamepad 0 and Gamepad 1 with identical values). This is likely due to how SDL2 enumerates the controller. Needs investigation.

## Testing Strategy

**Manual test utility** - Add `gamepad-test` command to CLI that:
- Initializes GamepadManager.
- Polls in a loop, prints state changes to console.
- Shows connected/disconnected events.
- Useful for verifying gamepad works on desktop and Pi.

Example usage:
```
$ ./build-debug/bin/cli gamepad-test
Gamepad test mode. Press Ctrl+C to exit.
[GamepadManager] SDL_INIT_GAMECONTROLLER initialized.
[Gamepad 0] Connected: Xbox Wireless Controller
[Gamepad 0] stick_x: 0.00  stick_y: 0.00  dpad: (0, 0)  A: false  B: false
[Gamepad 0] stick_x: -0.45 stick_y: 0.00  dpad: (0, 0)  A: false  B: false
[Gamepad 0] stick_x: -0.45 stick_y: 0.00  dpad: (0, 0)  A: true   B: false
[Gamepad 0] Disconnected
^C
```

No unit tests planned - the code is straightforward signal passing.

## Current Status (2024-12-31)

**Phase 1 Complete (Desktop + Pi):**
- `GamepadState.h` - State struct created.
- `GamepadManager.h/.cpp` - SDL2 wrapper with hot-plug support working.
- `cli gamepad-test` - Manual test utility working.
- SDL2 linked to dirtsim-core, conditional on availability.
- Tested on Linux desktop (Wayland) with Nintendo Switch Pro Controller.
- Tested on Pi with generic USB gamepad (Padix/iBuffalo SNES-style).
- SDL2 added to Yocto recipes (`libsdl2` in DEPENDS/RDEPENDS for both server and UI).
- CLI added to Yocto build and deploy script (`dirtsim-cli` on Pi).
- `dirtsim` user added to `input` group for gamepad access without sudo.
- Yocto build confirms: `SDL2 found - gamepad support enabled` (version 2.30.1).

**Known Issue - Duplicate Gamepad:**
Single controller appears as both Gamepad 0 and Gamepad 1 with identical values. Needs investigation - likely SDL2 enumeration quirk with how the controller exposes multiple interfaces.

**Known Issue - Server SIGTERM Handling:**
Server doesn't respond promptly to SIGTERM when SDL2 gamepad support is enabled. Causes slow service restarts (~60s timeout). Needs investigation - possibly SDL2 event loop blocking signals.

**Next Steps:**
1. Phase 2: Implement PlayerGooseBrain to control organism with gamepad.
2. Investigate duplicate gamepad issue.
3. Fix server signal handling for clean shutdown.

## Future Extensions

- Duck vs Goose selection via button or menu.
- UI indicator showing gamepad connection status.
- Analog stick provides proportional walk speed.
- Rumble/haptic feedback on collisions.
- Camera follow mode for player organism.
