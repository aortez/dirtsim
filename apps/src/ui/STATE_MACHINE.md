# UI State Machine

This UI is a small state machine. Most UI commands are state dependent, so
check the current state before driving screenshots or automation.

## Check current state

- `dirtsim-cli ui StatusGet` (includes `state`, `selected_icon`, `panel_visible`).
- `dirtsim-cli ui StateGet` (state only).

## States (basic)

- `Startup`: LVGL/display init, transitions automatically.
- `Disconnected`: not connected to server, shows diagnostics/retry UI.
- `StartMenu`: main hub with the icon rail and panels.
- `SimRunning`: simulation running.
- `Paused`: simulation paused.
- `TrainingIdle`: training config + icon rail.
- `TrainingActive`: live training progress (icon rail hidden).
- `TrainingUnsavedResult`: save/discard modal for completed training.
- `Shutdown`: app shutting down.

## Common transitions (basic)

- `Startup` -> `Disconnected` (init complete).
- `Disconnected` -> `StartMenu` (server connected).
- `StartMenu` -> `SimRunning` (`SimRun` / Start button).
- `SimRunning` -> `Paused` (`SimPause`).
- `Paused` -> `SimRunning` (`SimRun`).
- `SimRunning`/`Paused` -> `StartMenu` (`SimStop`).
- `StartMenu` -> `TrainingIdle` (`TrainingStart`).
- `TrainingIdle` -> `TrainingActive` (start evolution).
- `TrainingActive` -> `TrainingUnsavedResult` (training complete).
- `TrainingUnsavedResult` -> `TrainingIdle` (save/discard).
- `TrainingIdle`/`TrainingActive`/`TrainingUnsavedResult` -> `StartMenu` (stop/exit training).
- Any -> `Shutdown` (`Exit`).
