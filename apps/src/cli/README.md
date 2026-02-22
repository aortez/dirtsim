# DirtSim CLI Client

Command-line client for interacting with DirtSim server, UI, audio, and os-manager via WebSocket.

## Quick Start

**New to the CLI?** Try these commands first:

```bash
# 1. Launch everything (easiest way to get started)
./build-debug/bin/cli run-all

# 2. In another terminal, send a command
./build-debug/bin/cli server StatusGet

# 3. See a visual snapshot
./build-debug/bin/cli server DiagramGet

# 4. Take a screenshot
./build-debug/bin/cli screenshot output.png

# 5. Clean up when done
./build-debug/bin/cli cleanup
```

That's it! Now read below for details...

## Overview

The CLI provides several operation modes:

- **Command Mode**: Send individual commands to server, UI, audio, or os-manager
- **Run-All Mode**: Launch both server and UI with one command
- **Screenshot Mode**: Capture PNG screenshots from local or remote UI
- **Benchmark Mode**: Automated performance testing with metrics collection
- **Train Mode**: Run evolution training with JSON configuration
- **Cleanup Mode**: Find and gracefully shutdown rogue dirtsim processes
- **Functional Test Mode**: CLI-driven UI/server workflow checks
- **Integration Test Mode**: Automated server + UI lifecycle testing
- **Network Mode**: WiFi status, saved/open networks, connect, and forget (NetworkManager)

## Usage

### Command Mode

Send commands to the server, UI, audio, or os-manager:

```bash
# New fluent syntax: cli [target] [command] [params]
# Targets: 'server' (port 8080), 'ui' (port 7070), 'audio' (port 6060), or 'os-manager' (port 9090)

# Basic commands (no parameters)
./build-debug/bin/cli server StateGet
./build-debug/bin/cli server StatusGet
./build-debug/bin/cli ui StatusGet

# Commands with JSON parameters
./build-debug/bin/cli server SimRun '{"timestep": 0.016, "max_steps": 10}'
./build-debug/bin/cli server CellSet '{"x": 50, "y": 50, "material": "WATER", "fill": 1.0}'

# Get default JSON for a command
./build-debug/bin/cli server SimRun --example
./build-debug/bin/cli ui ScreenGrab --example

# Get ANSI color visualization (default; truecolor terminals)
./build-debug/bin/cli server DiagramGet

# Get emoji visualization
./build-debug/bin/cli server DiagramGet '{"style":"Emoji"}'

# Control simulation
./build-debug/bin/cli server Reset
./build-debug/bin/cli server Exit

# User settings (patch updates only specified fields)
./build-debug/bin/cli server UserSettingsGet
./build-debug/bin/cli server UserSettingsPatch '{"trainingSpec":{"scenarioId":"Clock","organismType":"DUCK","population":[]}}'
./build-debug/bin/cli server UserSettingsPatch '{"trainingSpec":{"scenarioId":"TreeGermination","organismType":"TREE","population":[]}}'

# UI commands
./build-debug/bin/cli ui SimPause
./build-debug/bin/cli ui SimStop

# OS manager commands
./build-debug/bin/cli os-manager SystemStatus
./build-debug/bin/cli os-manager WebUiAccessSet '{"enabled": true}'
./build-debug/bin/cli os-manager WebSocketAccessSet '{"enabled": true}'
./build-debug/bin/cli os-manager PeerClientKeyEnsure
./build-debug/bin/cli os-manager TrustBundleGet
./build-debug/bin/cli os-manager TrustPeer '{"bundle":{"host":"dirtsim2","ssh_user":"dirtsim","ssh_port":22,"host_fingerprint_sha256":"SHA256:...","client_pubkey":"ssh-ed25519 AAAA..."}}'
./build-debug/bin/cli os-manager UntrustPeer '{"host":"dirtsim2"}'
./build-debug/bin/cli os-manager StartServer
./build-debug/bin/cli os-manager StartAudio
./build-debug/bin/cli os-manager StopServer
./build-debug/bin/cli os-manager StopAudio
./build-debug/bin/cli os-manager RestartServer
./build-debug/bin/cli os-manager RestartAudio
./build-debug/bin/cli os-manager StartUi
./build-debug/bin/cli os-manager StopUi
./build-debug/bin/cli os-manager RestartUi
./build-debug/bin/cli os-manager Reboot

# Peer trust flow (bootstrap + trust)
./build-debug/bin/cli os-manager TrustBundleGet
./build-debug/bin/cli os-manager TrustPeer '{"bundle":{"host":"dirtsim2","ssh_user":"dirtsim","ssh_port":22,"host_fingerprint_sha256":"SHA256:...","client_pubkey":"ssh-ed25519 AAAA..."}}'

# Remote CLI execution (after trust)
./build-debug/bin/cli os-manager RemoteCliRun '{"host":"dirtsim2","args":["server","GenomeList"]}'

# Genome push/pull flow (next step)
# Pull: RemoteCliRun -> server GenomeGet on the target, then save JSON locally.
# Push: RemoteCliRun -> server GenomeSet on the target with the JSON payload.

# Audio commands
./build-debug/bin/cli audio StatusGet
./build-debug/bin/cli audio NoteOn '{\"frequency_hz\": 440, \"amplitude\": 0.5, \"duration_ms\": 120}'
./build-debug/bin/cli audio NoteOff

# Remote connections (override default addresses)
# Remote WebSockets require ?token=... when incoming WebSocket access is enabled.
# LAN Web UI enables incoming WebSocket access automatically.
# When LAN Web UI is disabled, use SSH and run dirtsim-cli locally on the device.
./build-debug/bin/cli server StateGet --address ws://dirtsim.local:8080
./build-debug/bin/cli server StateGet --address ws://dirtsim.local:8080?token=TOKEN
./build-debug/bin/cli ui StatusGet --address ws://dirtsim.local:7070
./build-debug/bin/cli ui StatusGet --address ws://dirtsim.local:7070?token=TOKEN
./build-debug/bin/cli os-manager SystemStatus --address ws://dirtsim.local:9090
./build-debug/bin/cli audio StatusGet --address ws://dirtsim.local:6060
```

### UI Navigation (State Machine)

The UI exposes a small state machine that you can drive via the CLI.
For a full UI overview, see `apps/src/ui/README.md`.

**Common state transitions:**

- `StartMenu` → `SimRunning`: `./build-debug/bin/cli ui SimRun`
- `SimRunning` → `Paused`: `./build-debug/bin/cli ui SimPause`
- `Paused` → `SimRunning`: `./build-debug/bin/cli ui SimRun`
- `SimRunning` or `Paused` → `StartMenu`: `./build-debug/bin/cli ui SimStop`
- `StartMenu` → `Training`: `./build-debug/bin/cli ui TrainingStart '{...}'`
- `Training` → `StartMenu`: `./build-debug/bin/cli ui TrainingQuit`
- `Training` → `Genome Browser panel`: `./build-debug/bin/cli ui GenomeBrowserOpen`
- `Training` → `SimRunning` (load genome): `./build-debug/bin/cli ui GenomeDetailLoad '{\"id\": \"...\"}'`
- Any → `Shutdown`: `./build-debug/bin/cli ui Exit`

**State inspection:**

```bash
./build-debug/bin/cli ui StateGet
./build-debug/bin/cli ui StatusGet
```

**Panel navigation inside SimRunning:**
There are no direct CLI commands for Scenario/Physics/Core panels yet.
TODO: Add explicit CLI navigation commands for these panels.

### Run-All Mode

Launch server, UI, and audio with a single command:

```bash
# Auto-detects display backend and launches all processes
./build-debug/bin/cli run-all
```

**What it does**:
- Auto-detects display backend (Wayland/X11)
- Launches server on port 8080
- Launches audio on port 6060
- Launches UI and auto-connects to server
- Monitors UI process
- Auto-shuts down audio and server when UI exits

**Use case**: Quickest way to launch everything for interactive testing.

**Note**: Runs in foreground - press Ctrl+C to exit both processes.

### Screenshot Mode

Capture PNG screenshots from the UI display:

```bash
# Capture from local UI (default: localhost:7070)
./build-debug/bin/cli screenshot output.png

# Capture from remote UI (Raspberry Pi)
./build-debug/bin/cli screenshot --address ws://dirtsim.local:7070 screenshot.png

# Custom timeout (default: 5000ms)
./build-debug/bin/cli screenshot --timeout 10000 output.png
```

**What it does**:
- Connects to UI via WebSocket (port 7070)
- Sends ScreenGrab command requesting PNG format
- Receives base64-encoded PNG data
- Decodes and writes to specified output file
- Reports image dimensions and file size

**Output example**:
```
✓ Screenshot saved to output.png (800x600, 45231 bytes)
```

**Use cases**:
- Capturing simulation states for documentation
- Automated testing with visual verification
- Recording evolution training progress
- Remote monitoring of headless Pi deployments

### UI Docs Screenshots

Capture the UI docs screenshots using the same CLI (intended for CI/runtime images):

```bash
# Capture all docs screens to /tmp/dirtsim-ui-docs
./build-debug/bin/cli docs-screenshots

# Override output directory
./build-debug/bin/cli docs-screenshots /tmp/dirtsim-ui-docs
```

**Env overrides**:
- `DIRTSIM_UI_ADDRESS` (default: ws://localhost:7070)
- `DIRTSIM_SERVER_ADDRESS` (default: ws://localhost:8080)
- `DIRTSIM_DOCS_SCREENSHOT_DIR` (default: /tmp/dirtsim-ui-docs)
- `DOCS_SCREENSHOT_ONLY` (comma-separated screen ids)
- `DOCS_SCREENSHOT_MIN_BYTES` (minimum size check, default: 2048)

### Functional Test Mode

Run a minimal UI/server workflow check against a running system:

```bash
# Default local ports (UI: 7070, server: 8080).
./build-debug/bin/cli functional-test canExit
./build-debug/bin/cli functional-test canExit --restart
./build-debug/bin/cli functional-test canTrain
./build-debug/bin/cli functional-test canSetGenerationsAndTrain
./build-debug/bin/cli functional-test canPlantTreeSeed
./build-debug/bin/cli functional-test canOpenTrainingConfigPanel
./build-debug/bin/cli functional-test canUpdateUserSettings
./build-debug/bin/cli functional-test canResetUserSettings
./build-debug/bin/cli functional-test canPersistUserSettingsAcrossRestart
./build-debug/bin/cli functional-test canUseDefaultScenarioWhenSimRunHasNoScenario
./build-debug/bin/cli functional-test canControlNesScenario
./build-debug/bin/cli functional-test canApplyClockTimezoneFromUserSettings
./build-debug/bin/cli functional-test canPlaySynthKeys
./build-debug/bin/cli functional-test verifyTraining

# verifyTraining runs 5 one-generation training loops with a 50-sized population,
# saving results between runs and verifying the genomes change.

# Note: canExit shuts down the UI, so run it last or restart the UI before other tests.
# Use --restart with canExit to relaunch local server/UI (skips remote addresses).

# Remote.
./build-debug/bin/cli functional-test canExit \
  --ui-address ws://dirtsim.local:7070 \
  --server-address ws://dirtsim.local:8080 \
  --os-manager-address ws://dirtsim.local:9090
```

**What it does**:
- Restarts UI and server via os-manager before tests.
- Connects to the UI and server.
- Queries server StatusGet and UI StateGet.
- Drives UI back to StartMenu if needed (SimStop).
- Sends UI Exit.
- Restarts the UI and server via os-manager after tests complete.
- For canTrain: runs TrainingStart with defaults, waits for UnsavedTrainingResult, saves all candidates, then requests TrainingResultList/TrainingResultGet for the newest session.
- For canSetGenerationsAndTrain: runs TrainingStart with max_generations=2, verifies the training result reports the expected completed/max generations.
- For canPlantTreeSeed: starts Tree Germination, plants a seed via the UI API, and waits for tree_vision.
- For canOpenTrainingConfigPanel: starts training, opens the Training config panel via UI API, and verifies UI still responds.
- For canUpdateUserSettings: updates timezone/volume/default scenario via server UserSettings APIs and verifies with UserSettingsGet.
- For canResetUserSettings: changes settings, runs UserSettingsReset, and verifies defaults are restored.
- For canPersistUserSettingsAcrossRestart: updates settings, restarts services, and verifies settings persisted.
- For canUseDefaultScenarioWhenSimRunHasNoScenario: sets default scenario and verifies UI SimRun (without scenario_id) uses it.
- For canControlNesScenario: starts NES scenario, verifies timestep advances, sets Start via server `NesInputSet`, then releases it.
- For canApplyClockTimezoneFromUserSettings: sets timezone, runs Clock scenario, and verifies pushed Clock config uses the setting.
- For canPlaySynthKeys: opens the Synth screen and sends programmatic key press/release events via UI API, verifying state details.

### Network Mode

Query WiFi state and manage saved/open networks:

```bash
# Status (connected SSID if present)
./build-debug/bin/cli network status

# Saved + open networks
./build-debug/bin/cli network list

# Connect to a saved or open network by SSID
./build-debug/bin/cli network connect "MySSID"

# Connect with password (WPA2/WPA3)
./build-debug/bin/cli network connect "MySSID" --password "secret"

# Scan access points with channel/BSSID info
./build-debug/bin/cli network scan

# Disconnect active WiFi (optionally by SSID)
./build-debug/bin/cli network disconnect
./build-debug/bin/cli network disconnect "MySSID"

# Forget a saved network (removes autoconnect profile)
./build-debug/bin/cli network forget "MySSID"
```

**Output**: JSON only (stdout), errors to stderr.

**Note**: These commands run locally and require NetworkManager (`libnm`).

### Benchmark Mode

Automated performance testing with server auto-launch:

```bash
# Basic benchmark (headless server, default: benchmark scenario, 120 steps)
# Use release build for accurate performance measurements!
./build-release/bin/cli benchmark --steps 120

# Different scenario
./build-release/bin/cli benchmark --scenario sandbox --steps 120

# Custom world size (default: scenario default)
./build-release/bin/cli benchmark --world-size 150 --steps 120

# Full control: scenario, world size, and step count
./build-release/bin/cli benchmark --scenario sandbox --world-size 150 --steps 1000
```

**Output**: Clean JSON results including:
- Scenario name and grid size
- Total duration
- Server FPS
- Physics timing (avg/total/calls)
- Serialization timing
- Detailed timer statistics (subsystem breakdown: cohesion, adhesion, pressure, etc.)

**Features**:
- Server runs with logging disabled (`--log-config benchmark-logging-config.json`) for clean output
- Client logs suppressed (use `--verbose` to see debug info)
- Pure JSON output suitable for piping to `jq` or CI/CD tools

**Example output:**
```json
{
  "scenario": "sandbox",
  "steps": 120,
  "server_physics_avg_ms": 0.52,
  "timer_stats": {
    "cohesion_calculation": {"avg_ms": 0.09, "total_ms": 10.8, "calls": 120},
    "adhesion_calculation": {"avg_ms": 0.03, "total_ms": 3.6, "calls": 120},
    "resolve_forces": {"avg_ms": 0.28, "total_ms": 33.6, "calls": 120}
  }
}
```

**Using for optimization testing:**
```bash
# Baseline measurement (use release build!)
./build-release/bin/cli benchmark --steps 1000 > baseline.json

# After optimization
./build-release/bin/cli benchmark --steps 1000 > optimized.json

# Compare specific subsystems
jq '.timer_stats.cohesion_calculation.avg_ms' baseline.json optimized.json
```

### Train Mode

Run evolution training with JSON configuration:

```bash
# Train with default config (100 generations, 50 population)
./build-debug/bin/cli train

# Train with custom config
./build-debug/bin/cli train '{
  "evolution": {
    "maxGenerations": 10,
    "populationSize": 20,
    "tournamentSize": 3
  },
  "scenarioId": "TreeGermination",
  "organismType": "TREE",
  "resumePolicy": "WarmFromBest",
  "population": [
    {
      "brainKind": "NeuralNet",
      "count": 20,
      "randomCount": 20
    }
  ]
}'

# Train ducks on Clock scenario
./build-debug/bin/cli train '{
  "evolution": {
    "maxGenerations": 10,
    "populationSize": 20,
    "tournamentSize": 3
  },
  "scenarioId": "Clock",
  "organismType": "DUCK",
  "resumePolicy": "WarmFromBest",
  "population": [
    {
      "brainKind": "NeuralNet",
      "count": 20,
      "randomCount": 20
    }
  ]
}'

# Train on remote server
./build-debug/bin/cli train --address ws://dirtsim.local:8080 '{"evolution": {"maxGenerations": 5}}'
```

**Config JSON** maps to `EvolutionStart::Command`:
```json
{
  "evolution": {
    "populationSize": 50,
    "tournamentSize": 3,
    "maxGenerations": 100,
    "maxSimulationTime": 600.0,
    "energyReference": 100.0
  },
  "mutation": {
    "rate": 0.015,
    "sigma": 0.05,
    "resetRate": 0.0005
  },
  "scenarioId": "TreeGermination",
  "organismType": "TREE",
  "population": [
    {
      "brainKind": "NeuralNet",
      "count": 50,
      "randomCount": 50
    }
  ]
}
```

**Output**: Progress bars during training, JSON results on completion:
```json
{
  "completed": true,
  "totalGenerations": 10,
  "populationSize": 20,
  "bestFitnessAllTime": 2.5,
  "durationSec": 45.2,
  "bestGenomeId": 42
}
```

**Features**:
- Real-time progress bar showing generation and evaluation progress.
- Auto-launches server if no `--address` specified.
- Clean JSON output for scripting.

### Progress Mode

Watch live training progress from a running server:

```bash
# Local server
./build-debug/bin/cli progress

# Remote server
./build-debug/bin/cli progress --address ws://garden.local:8080

# One-shot sample (10 seconds)
timeout 10 ./build-debug/bin/cli progress --address ws://garden.local:8080
```

Sample output:

```text
gen=3/inf eval=42/100 genomes=71 capPerOrganismBrain=5000 genBest=2.731234 allBest=2.812000 avg=0.664900 src=offspring_mutated bestGenome=1c515ca1
bestCmdHistogram genome=1c515ca1 accepted=118 rejected=42 source=topSignatures Wait=56 GrowLeaf=31 GrowRoot=19 GrowWood=7 ProduceSeed=5
```

Use `watch` for a raw JSON stream of all server broadcasts.

### Cleanup Mode

Find and gracefully shutdown rogue dirtsim processes:

```bash
# Clean up all dirtsim processes
./build-debug/bin/cli cleanup
```

**Shutdown cascade** (tries each method in order):
1. **WebSocket API** - Send Exit command (most graceful)
   - Server: `ws://localhost:8080`
   - UI: `ws://localhost:7070`
2. **SIGTERM** - Graceful OS signal
3. **SIGKILL** - Force kill (last resort)

**All waits exit early** if process dies before timeout.

**Performance**: Typical cleanup time is under 500ms. WebSocket shutdown usually completes in 200-400ms.

**Use cases:**
- Clean up after crashes during development
- Ensure clean slate before running benchmarks or tests
- Fix "port already in use" errors

## Architecture

### Components

**BenchmarkRunner** (`BenchmarkRunner.{h,cpp}`)
- Launches server subprocess
- Runs simulation and collects metrics
- Supports UI client simulation mode

**SubprocessManager** (`SubprocessManager.{h,cpp}`)
- RAII wrapper for fork/exec/kill
- Manages both server and UI subprocesses
- Handles graceful shutdown (SIGTERM) with SIGKILL fallback

**WebSocketClient** (`WebSocketClient.{h,cpp}`)
- Dual-mode WebSocket client (blocking + async)
- Supports JSON and binary (zpp_bits) messages
- 10MB message size limit for large WorldData

### Communication Protocols

**JSON Commands** (text):
```json
{"command": "state_get"}
{"command": "sim_run", "timestep": 0.016, "max_steps": 100}
```

**Binary Messages** (zpp_bits):
- WorldData serialized with zpp_bits for efficiency
- Automatically unpacked to JSON for compatibility

From C++, use binary serialization.  JSON is for web apps.

**Notes**:
- `sim_run` creates World and transitions Idle → SimRunning
- Set `max_steps` to control simulation duration:
  - `-1` = unlimited (runs until paused or stopped)
  - `>0` = runs that many steps then transitions to SimPaused
- `exit` works from any state

## Troubleshooting

### "Failed to connect to ws://localhost:8080"

Server not running? Launch it:
```bash
./build-debug/bin/cli run-all
```

Or check what's running:
```bash
pgrep -fa dirtsim
```

### "Port already in use"

Clean up any rogue processes:
```bash
./build-debug/bin/cli cleanup
```

### Cleanup seems slow or hangs

If `run-all` is running in another terminal, it monitors the UI and won't let the server or audio exit until the UI closes. Either:
- Use Ctrl+C on the `run-all` terminal first
- Or just use `cleanup` - it will force shutdown with SIGTERM/SIGKILL

### Response timeout errors

Increase timeout for slow operations:
```bash
./build-debug/bin/cli --timeout 10000 server StateGet  # 10 second timeout
```

## Use Cases

Example:
```bash
# Performance regression testing.
./build-release/bin/cli benchmark --steps 120 > benchmark_results.json

# Sanity check (debug build is fine)
./build-debug/bin/cli functional-test canTrain || exit 1
```

### Always Cleanup

The cleanup command is robust and handles edge cases:

```bash
# Gracefully shuts down ALL dirtsim processes
./build-debug/bin/cli cleanup

# Shows which method worked:
# ✓ WebSocket API (most graceful)
# ✓ SIGTERM (graceful signal)
# ✓ SIGKILL (force kill)
```

### Auto-Generated Command List

The CLI automatically discovers all server and UI commands at compile-time.
Check the help to see what's available:

```bash
# Always up-to-date with actual server/UI capabilities
./build-debug/bin/cli --help
```

When new commands are added to the server or UI, they automatically appear in the CLI help.
