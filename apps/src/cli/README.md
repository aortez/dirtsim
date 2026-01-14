# DirtSim CLI Client

Command-line client for interacting with DirtSim server and UI via WebSocket.

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

- **Command Mode**: Send individual commands to server or UI
- **Run-All Mode**: Launch both server and UI with one command
- **Screenshot Mode**: Capture PNG screenshots from local or remote UI
- **Benchmark Mode**: Automated performance testing with metrics collection
- **Train Mode**: Run evolution training with JSON configuration
- **Cleanup Mode**: Find and gracefully shutdown rogue dirtsim processes
- **Integration Test Mode**: Automated server + UI lifecycle testing

## Usage

### Command Mode

Send commands to the server or UI:

```bash
# New fluent syntax: cli [target] [command] [params]
# Targets: 'server' (port 8080) or 'ui' (port 7070)

# Basic commands (no parameters)
./build-debug/bin/cli server StateGet
./build-debug/bin/cli server StatusGet
./build-debug/bin/cli ui StatusGet

# Commands with JSON parameters
./build-debug/bin/cli server SimRun '{"timestep": 0.016, "max_steps": 10}'
./build-debug/bin/cli server CellSet '{"x": 50, "y": 50, "material": "WATER", "fill": 1.0}'

# Get emoji visualization
./build-debug/bin/cli server DiagramGet

# Control simulation
./build-debug/bin/cli server Reset
./build-debug/bin/cli server Exit

# UI commands
./build-debug/bin/cli ui SimPause
./build-debug/bin/cli ui SimStop

# Remote connections (override default addresses)
./build-debug/bin/cli server StateGet --address ws://dirtsim.local:8080
./build-debug/bin/cli ui StatusGet --address ws://dirtsim.local:7070
```

### Run-All Mode

Launch both server and UI with a single command:

```bash
# Auto-detects display backend and launches both processes
./build-debug/bin/cli run-all
```

**What it does**:
- Auto-detects display backend (Wayland/X11)
- Launches server on port 8080
- Launches UI and auto-connects to server
- Monitors UI process
- Auto-shuts down server when UI exits

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
  "scenarioId": "TreeGermination"
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
  "scenarioId": "TreeGermination"
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

### Integration Test Mode

Automated end-to-end testing:

```bash
./build-debug/bin/cli integration_test
```

**What it does**:
1. Launches server on port 8080
2. Launches UI with Wayland backend, auto-connects to server
3. Starts simulation with `sim_run` (1 step)
4. Sends `exit` command to server
5. Shuts down UI
6. Verifies clean shutdown of both processes

**Exit Codes**:
- `0`: All tests passed
- `1`: Test failed (check stderr for details)

## Architecture

### Components

**IntegrationTest** (`IntegrationTest.{h,cpp}`)
- Orchestrates server + UI launch and testing
- Manages full lifecycle from launch to cleanup
- Returns exit code for CI/CD integration

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

If `run-all` is running in another terminal, it monitors the UI and won't let the server exit until the UI closes. Either:
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
./build-debug/bin/cli integration_test || exit 1
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
