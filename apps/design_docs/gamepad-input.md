# Gamepad Input for Organism Control

## Summary

Add gamepad support to directly control organisms (like a platformer). When a gamepad connects, spawn a player-controlled Duck that responds to D-pad/stick for movement and buttons for jump.

## Requirements

1. **Cross-platform**: Linux desktop (Wayland/X11) and Raspberry Pi (FBDEV).
2. **Direct organism control**: D-pad/stick controls movement, button for jump.
3. **Player-spawned organism**: Gamepad spawns and controls its own dedicated creature.

## Key Design Decisions

1. **Implement in server process** (not UI) - Organisms and brains live server-side, avoids WebSocket latency.
2. **SDL2 GameController API** - See rationale below.
3. **PlayerDuckBrain** - New brain that reads gamepad state instead of AI.
4. **Gamepad spawns its own organism** - When button pressed, creates a dedicated Duck.
5. **GamepadManager owned by Server::StateMachine** - Clean ownership, easy access from states.
6. **Poll every physics tick (~60Hz)** - Simple, consistent, plenty fast for gamepad input.
7. **Multi-gamepad support from the start** - Each gamepad spawns its own Duck.

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
  PlayerDuckBrain (receives gamepad state)
       |
       v
  Duck (organism with PlayerDuckBrain)
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

### Phase 2: PlayerDuckBrain

**New file: `src/core/organisms/PlayerDuckBrain.h/.cpp`**

```cpp
class PlayerDuckBrain : public DuckBrain {
public:
    explicit PlayerDuckBrain(const GamepadState* state);
    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override;
private:
    const GamepadState* gamepad_state_;
    bool last_jump_pressed_ = false;  // Edge detection for B button.
};
```

Implementation:
- Read `dpad_x` or `stick_x` for movement direction.
- D-pad/stick left → `DuckAction::RUN_LEFT`.
- D-pad/stick right → `DuckAction::RUN_RIGHT`.
- Neutral → `DuckAction::WAIT`.
- A button (edge-detected, on ground) → `DuckAction::JUMP`.

### Phase 3: Server Integration

**Add GamepadManager to StateMachine:**
```cpp
// In StateMachine::Impl
std::unique_ptr<GamepadManager> gamepadManager_;

// Accessor
GamepadManager* getGamepadManager();
```

**Modify SimRunning state:**
```cpp
// In SimRunning
std::map<size_t, OrganismId> gamepad_to_duck_;  // gamepad_index → duck_id
std::map<size_t, bool> prev_any_button_;        // For edge detection of spawn.

void SimRunning::tick(StateMachine& dsm) {
    // Poll gamepad at start of each physics tick.
    auto& gm = dsm.getGamepadManager();
    gm.poll();

    // Handle disconnects → remove duck.
    for (size_t idx : gm.getNewlyDisconnected()) {
        if (gamepad_to_duck_.count(idx)) {
            world->getOrganismManager().removeOrganismFromWorld(*world, gamepad_to_duck_[idx]);
            gamepad_to_duck_.erase(idx);
        }
        prev_any_button_.erase(idx);
    }

    // Process each connected gamepad.
    for (size_t i = 0; i < gm.getDeviceCount(); i++) {
        auto* state = gm.getGamepadState(i);
        if (!state || !state->connected) continue;

        // Check for any button press (edge-detected) to spawn duck.
        bool prev_any = prev_any_button_[i];
        bool curr_any = state->button_a || state->button_b;
        if (curr_any && !prev_any && !gamepad_to_duck_.count(i)) {
            spawnPlayerDuck(i);
        }
        prev_any_button_[i] = curr_any;

        // Pass gamepad input to existing duck's brain.
        if (gamepad_to_duck_.count(i)) {
            if (auto* duck = world->getOrganismManager().getDuck(gamepad_to_duck_[i])) {
                if (auto* brain = duck->getBrain()) {
                    brain->setGamepadInput(*state);
                }
            } else {
                gamepad_to_duck_.erase(i);  // Duck died, clean up.
            }
        }
    }

    // ... existing physics ...
}

void SimRunning::spawnPlayerDuck(size_t gamepad_index) {
    // Spawn at center-top of world.
    uint32_t x = world->getData().width / 2;
    uint32_t y = 2;

    auto brain = std::make_unique<PlayerDuckBrain>();
    OrganismId id = world->getOrganismManager().createDuck(*world, x, y, std::move(brain));
    gamepad_to_duck_[gamepad_index] = id;
}
```

**Behavior:**
- One duck per gamepad (stored in `gamepad_to_duck_` map).
- Any button press spawns duck (if none exists for this gamepad).
- After spawning, buttons control the duck (A for jump, D-pad/stick for movement).
- Respawning: press any button again after duck dies/is removed.
- Scenario switch clears organisms (including player ducks) - map should be cleared too.
- Gamepad disconnect removes the duck.

### Phase 4: Build System

Already complete from Phase 1:
- SDL2 conditionally linked to dirtsim-core.
- `DIRTSIM_HAS_GAMEPAD` compile definition set when SDL2 found.
- GamepadManager.cpp only compiled when SDL2 available.

## Files to Create

| File | Purpose | Status |
|------|---------|--------|
| `src/core/input/GamepadState.h` | Gamepad state struct. | ✅ Done |
| `src/core/input/GamepadManager.h` | Manager header (owns SDL subsystem + devices). | ✅ Done |
| `src/core/input/GamepadManager.cpp` | Manager implementation. | ✅ Done |
| `src/core/organisms/PlayerDuckBrain.h` | Player brain header. | ✅ Done |
| `src/core/organisms/PlayerDuckBrain.cpp` | Player brain implementation. | ✅ Done |

## Files to Modify

| File | Changes | Status |
|------|---------|--------|
| `src/server/StateMachine.h` | Add GamepadManager member + accessor. | ✅ Done |
| `src/server/StateMachine.cpp` | Initialize GamepadManager in Impl. | ✅ Done |
| `src/server/states/SimRunning.h` | Add `gamepad_to_duck_` and `prev_any_button_` maps. | ✅ Done |
| `src/server/states/SimRunning.cpp` | Poll gamepad in tick(), spawn player duck on any button. | ✅ Done |
| `src/cli/main.cpp` | Add `gamepad-test` command. | ✅ Done |
| `CMakeLists.txt` | Add PlayerDuckBrain source files. | ✅ Done |

## Control Mapping

| Input | Action |
|-------|--------|
| D-pad Left / Left Stick Left | Run left (when duck exists). |
| D-pad Right / Left Stick Right | Run right (when duck exists). |
| Any Button (first press) | Spawn duck (if none exists for this gamepad). |
| A Button (South) | Jump (when on ground, after duck spawned). |
| B Button (East) | Future use. |

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

## Current Status (2025-12-31)

**✅ All Phases Complete:**

**Phase 1 - Gamepad Abstraction (Complete):**
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

**Phase 2 - PlayerDuckBrain (Complete):**
- `PlayerDuckBrain.h/.cpp` - Duck brain that responds to gamepad input.
- D-pad/stick controls horizontal movement (RUN_LEFT/RUN_RIGHT/WAIT).
- A button triggers jump (edge-detected, only when on ground).
- Brain receives input via `setGamepadInput()` each tick.

**Phase 3 - Server Integration (Complete):**
- GamepadManager owned by StateMachine, accessible to all states.
- SimRunning polls gamepad each physics tick (~60Hz).
- Any button press spawns player-controlled duck (if none exists for that gamepad).
- After spawning, buttons control the duck (A for jump, D-pad/stick for movement).
- Gamepad disconnect removes the associated duck.
- Duck death allows respawn with another button press.
- Multi-gamepad support: each gamepad spawns and controls its own duck.

**Recent Fixes:**
1. **Duplicate gamepad reporting** (FIXED 2025-12-31): Added joystick_id deduplication check in `handleControllerAdded()`. Controllers are now tracked by unique joystick_id to prevent opening the same physical device multiple times when device_index differs from instance_id.
2. **Server SIGTERM handling** (FIXED 2025-12-31): Made frame rate limiter sleep interruptible by breaking it into 5ms chunks with `shouldExit()` checks. Server now responds to SIGTERM within 5ms instead of up to 60s.

## Future Extensions

- Organism selection via button or menu (Duck, Goose, etc.).
- UI indicator showing gamepad connection status.
- Analog stick provides proportional walk speed.
- Rumble/haptic feedback on collisions.
- Camera follow mode for player organism.
