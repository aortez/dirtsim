# Air Steering Implementation Notes

## Goal
Add SMB1-style limited air control to the duck's jumping physics.

## Background: SMB1 Mechanics
From speedrun research (https://www.speedrun.com/smb1/guides/pbl9d):

- **Asymmetric acceleration**: You accelerate faster in the direction you're NOT facing.
- **Limited air control**: You can influence horizontal velocity mid-air, but it's reduced compared to ground control.
- **Speed ceiling lock**: If you drop below max walking speed mid-air, you can't exceed it until landing.
- **Backward jump trick**: Jump facing backward, steer forward = faster acceleration (exploits asymmetry).

## Current State

**No air control exists.** The duck only applies walking force when on ground:

```cpp
// In Duck::applyMovementToCell() around line 205:
if (on_ground_ && std::abs(walk_direction) > 0.01f) {
    Vector2d walk_force(walk_direction * WALK_FORCE, 0.0);
    cell.addPendingForce(walk_force);
}
```

## Tests Added (commit 493f9ba)

Location: `src/core/organisms/tests/Duck_test.cpp`

1. **`AirSteeringForwardWhileMovingForward`** - Documents baseline. Jumps while holding forward, records velocity change. Currently shows -15.63 (pure air resistance).

2. **`AirSteeringBackwardDecelsFasterThanForward`** - **FAILS until implemented.** Runs two identical jumps (one with forward input, one with backward), asserts backward causes >2 more deceleration. Currently both show -15.63 (no difference).

Also added:
- `DuckTestSetup` helper struct for common test setup.
- `TestDuckBrain` direct input mode (`setDirectInput`, `setMove`, `triggerJump`).

## Implementation

Modify `Duck::applyMovementToCell()` in `src/core/organisms/Duck.cpp`:

```cpp
// Simple version - apply reduced force when airborne:
if (std::abs(walk_direction) > 0.01f) {
    float multiplier = on_ground_ ? 1.0f : 0.4f;  // 40% air control
    Vector2d walk_force(walk_direction * WALK_FORCE * multiplier, 0.0);
    cell.addPendingForce(walk_force);
}
```

For full SMB1 feel, could also add:
- Asymmetric acceleration (faster when input opposes facing direction).
- Speed ceiling tracking (cap air speed if it drops below walk speed).

## Running the Tests

```bash
cd /home/data/workspace/dirtsim/apps
make debug
./build-debug/bin/dirtsim-tests --gtest_filter="*AirSteering*"
```

Expected output before implementation:
```
Forward input:  vel_change = -15.63
Backward input: vel_change = -15.63
Difference: 0.00
FAILED
```

Expected after implementation:
```
Forward input:  vel_change = -12.xx (less decel, input fights air resistance)
Backward input: vel_change = -18.xx (more decel, input adds to air resistance)
Difference: -6.xx
PASSED
```
