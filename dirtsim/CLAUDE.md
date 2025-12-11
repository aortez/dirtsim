# CLAUDE.md - dirtsim Application

This is the main simulation application. For repo-level overview, see `../CLAUDE.md`.

## Quick Reference

- **Build**: `make debug` or `make release`
- **Run**: `./build-debug/bin/cli run-all`
- **Test**: `make test`
- **Format**: `make format`
- **Deploy**: See `../yocto/` for Yocto-based Pi deployment (`npm run yolo`)

## Building
```bash
# Build debug version (outputs to build-debug/).
make debug

# Build optimized release version (outputs to build-release/).
make release

# Make the unit tests.
make build-tests

# Format source code.
make format

# Clean build artifacts (removes both build-debug/ and build-release/).
make clean

# Show all available targets.
make help

# Manual CMake build (if needed).
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug
make -C build-debug -j12

# Note: Debug and release builds use separate directories to avoid conflicts.
# - build-debug/  - Debug builds (-O0 -g)
# - build-release/ - Release builds (-O3 optimizations)
```

### Running
```bash
# Run both client and server (debug build).
./build-debug/bin/cli run-all

# CLI integration test (quick, verifies ui, server, and cli).
./build-debug/bin/cli integration_test

# Easy clean of up all sparkle-duck processes.
./build-debug/bin/cli cleanup

# Run benchmark and output results to file.
./build-release/bin/cli benchmark > benchmark.json && cat benchmark.json | jq .server_fps

# Sending commands (new fluent syntax: cli [target] [command] [params]).
./build-debug/bin/cli server StateGet
./build-debug/bin/cli server SimRun '{"timestep": 0.016, "max_steps": 1}'
./build-debug/bin/cli server DiagramGet

# Run everything.
./build-debug/bin/cli run-all
```

### CLI documentation
src/cli/README.md

### Testing
```bash
# Run all unit tests (uses debug build).
make test

# Run tests with filters using ARGS.
make test ARGS='--gtest_filter=State*'

# Run state machine tests directly.
./build-debug/bin/sparkle-duck-tests --gtest_filter="StateIdle*"
./build-debug/bin/sparkle-duck-tests --gtest_filter="StateSimRunning*"

# List all available tests.
./build-debug/bin/sparkle-duck-tests --gtest_list_tests

# Run specific test.
./build-debug/bin/sparkle-duck-tests --gtest_filter="StateSimRunningTest.AdvanceSimulation_StepsPhysicsAndDirtFalls"
```

### Debugging and Logging

#### Log Outputs
- **Console**: INFO level and above (colored output)
- **File**: DEBUG/TRACE level and above (`sparkle-duck.log`)

#### Log Files
- **File**: `sparkle-duck.log` main application and unit tests
- **Location**: Same directory as executable
- **Behavior**: File is truncated at startup for fresh logs each session

#### Log Levels

**Setting log levels** via `--log-level` flag:
```bash
# Server with debug logging
./build/bin/sparkle-duck-server --log-level debug -p 8080

# UI with trace logging
./build/bin/sparkle-duck-ui --log-level trace -b wayland

# Use with run_debug.sh
./run_debug.sh -l debug      # Debug level
./run_debug.sh -l trace      # Maximum verbosity
./run_debug.sh --log-level info  # Default

# Valid levels: trace, debug, info, warn, error, critical, off
```

**What each level means:**
- **INFO**: Important events (startup, world creation, user interactions)
- **DEBUG**: Moderate frequency events (drag updates, timestep tracking)
- **TRACE**: High-frequency per-frame events (pressure systems, physics details)

You can troubleshoot behavior by examining the TRACE logs.

#### Core Dumps for Crash Analysis
When applications crash with segmentation faults, core dumps provide invaluable debugging information.

**Locating Core Dumps:**
```bash
# List recent core dumps (systemd-coredump)
coredumpctl list | grep sparkle-duck | tail -5

# Get info about the latest crash
coredumpctl info

# Analyze with gdb
coredumpctl gdb <PID>

# Quick backtrace from most recent crash
coredumpctl gdb --batch -ex "bt" -ex "quit"
```

**Analyzing Core Dumps:**
```bash
# Get backtrace of all threads
coredumpctl gdb <PID>
(gdb) bt            # Main thread backtrace
(gdb) info threads  # List all threads
(gdb) thread apply all bt  # Backtrace of all threads
(gdb) frame 5       # Jump to specific frame
(gdb) print variable_name  # Inspect variables
(gdb) quit

# Or use batch mode for automated analysis
cat > /tmp/gdb_commands.txt << 'EOF'
set pagination off
bt
info threads
thread apply all bt
quit
EOF

coredumpctl gdb <PID> < /tmp/gdb_commands.txt > crash_analysis.txt 2>&1
```

## Architecture

### Component Libraries

The project is organized into three component libraries:

- **sparkle-duck-core**: Shared types for serialization (MaterialType, Vector2d/i, Cell, WorldData, ScenarioConfig, RenderMessage)
- **sparkle-duck-server**: Physics engine (World + calculators), server logic, scenarios, server API commands
- **sparkle-duck-ui**: UI components (controls, rendering, LVGL builders), UI state machine, UI API commands

Executables (server, UI, CLI, tests) link against these libraries.

### Physics System

- **World**: Grid-based physics simulation with pure-material cells
- **Cell**: Fill ratio [0,1] with single material type per cell
- **MaterialType**: Enum-based material system (AIR, DIRT, LEAF, METAL, SAND, SEED, WALL, WATER, WOOD)
- **Material Properties**: Each material has density, cohesion, adhesion, viscosity, friction, elasticity

### Core Components
- **Vector2d**: 2D floating point vector class
- **Vector2i**: 2D integer vector class
- **Server::StateMachine**: Aka DirtSimStateMachine (DSSM). Headless server state machine (Idle → SimRunning ↔ SimPaused → Shutdown)
- **Ui::StateMachine**: UI client state machine (Disconnected → StartMenu → SimRunning ↔ Paused → Shutdown)
- **WorldEventGenerator**: Strategy pattern for initial world setup and dynamic particle generation
- **ScenarioRegistry**: Registry of available scenarios (owned by each StateMachine)
- **EventSink**: Interface pattern for clean event routing

### UI Framework
- **ControlPanel**: LVGL controls for scenario toggles and simulation parameters
- **CellRenderer**: Renders world state to LVGL canvases
- **NeuralGridRenderer**: Renders 15×15 tree organism perception (side-by-side with world)
- **UiComponentManager**: Manages LVGL screen and containers (50/50 split layout)
- **WebSocketClient**: Connects to DSSM server for world data
- **WebSocketServer**: Accepts remote control commands (port 7070)

### Physics Overview

- Pure material cells with fill ratios [0,1]
- Material-specific density affecting gravity response
- Center of mass (COM) physics within [-1,1] bounds
- 9 material types: AIR, DIRT, LEAF, METAL, SAND, SEED, WALL, WATER, WOOD
- Cohesion (same-material attraction) and adhesion (different-material attraction)
- Viscosity and friction (static/kinetic)
- Pressure systems: hydrostatic, dynamic, diffusion
- Air resistance
- Tree organisms with organism_id tracking

## Testing Framework

Uses GoogleTest for unit and state machine testing.

### State Machine Tests
- **StateIdle_test.cpp**: Tests Idle state event handlers (SimRun, Exit)
- **StateSimRunning_test.cpp**: Tests SimRunning state (physics, toggles, API commands)
- One happy-path test per event handler
- Tests verify state transitions, callbacks, and world state changes

### Core Tests
- **Vector2d_test.cpp**: 2D mathematics validation
- **Vector2i_test.cpp**: 2D integer vector operations
- **ResultTest.cpp**: Result<> class behavior
- **TimersTest.cpp**: Timing infrastructure

### Integration Testing
- **CLI client** can send commands to server or UI for scripted testing
- WebSocket API enables external test drivers (Python, bash scripts, etc.)
- Server and UI can be tested independently

## Performance Testing

### Benchmark Tool
The CLI tool includes a benchmark mode for measuring physics performance:

```bash
# Basic benchmark (headless server, 120 steps)
# Use release build for accurate performance!
./build-release/bin/cli benchmark --steps 120

# Simulate UI client load (realistic with frame_ready responses)
./build-release/bin/cli benchmark --steps 120 --simulate-ui

# Different scenario
./build-release/bin/cli benchmark --scenario dam_break --steps 120
```

## Code Formatter
The code formatter will run via hook, but you can also run it like so:
```bash
make format
```

The benchmark auto-launches the server, runs the simulation, collects performance metrics from both server and client, then outputs JSON results including FPS, physics timing, serialization timing, round-trip latencies, and detailed timer statistics for each physics subsystem (cohesion, adhesion, pressure diffusion, etc.).

## Coding Practices

### Serialization

**Use ReflectSerializer for automatic JSON conversion:**
```cpp
// Define aggregate struct
struct MyData {
    int x = 0;
    double y = 0.0;
    std::string name;
};

// Automatic serialization (zero boilerplate!)
MyData data{42, 3.14, "test"};
nlohmann::json j = ReflectSerializer::to_json(data);
MyData data2 = ReflectSerializer::from_json<MyData>(j);
```

ReflectSerializer uses qlibs/reflect for compile-time introspection. It works automatically with any aggregate type - no manual field listing needed. See existing usage in Cell, WorldData, Vector2d, and API command/response types.

For C++ APIs, always start with the C++ object, populate it with designated initializers when possible, then convert it as needed in the sending later.

## Development Environment

### Display Backends
Supports SDL, Wayland, X11, and Linux FBDEV backends. Primary development is Linux-focused with Wayland backend as default.

### Deployment to Raspberry Pi

The project uses Yocto to build complete bootable images for the Pi.

**Deploy from workstation:**
```bash
cd ../yocto
npm run yolo -- --hold-my-mead          # Build + deploy + reboot
npm run yolo -- --clean-all --hold-my-mead  # Full rebuild + deploy
```

**Check service status on Pi:**
```bash
ssh dirtsim.local
systemctl status sparkle-duck-server    # Check status
journalctl -u sparkle-duck-server -f    # Follow logs
```

**Server details:**
- Runs as system service (starts at boot).
- User: `dirtsim`
- Port: 8080 (WebSocket), 8081 (HTTP dashboard)
- Logs: `/home/dirtsim/sparkle-duck/sparkle-duck.log`
- Service file: `/usr/lib/systemd/system/sparkle-duck-server.service`

See `../yocto/` for full Yocto layer documentation and image customization.

### Remote CLI Control

The app communicates with two WebSocket endpoints:
- **Port 8080** (Server): Physics simulation control, world state queries
- **Port 7070** (UI): UI state machine control, display settings

**Check if service is running:**
```bash
./build-debug/bin/cli ui StatusGet --address ws://dirtsim.local:7070
# Returns: {"state":"StartMenu","connected_to_server":true,"fps":0.0,...}
```

**Shutdown the remote service:**
```bash
./build-debug/bin/cli ui Exit --address ws://dirtsim.local:7070
# Returns: {"success":true}
```

**Important:** To start a simulation remotely, send commands to the **UI** (port 7070), not the server:
```bash
# Start simulation (UI coordinates with server)
./build-debug/bin/cli ui SimRun '{"scenario_id": "sandbox"}' --address ws://dirtsim.local:7070

# Query world state (server)
./build-debug/bin/cli server DiagramGet --address ws://dirtsim.local:8080
./build-debug/bin/cli server StateGet --address ws://dirtsim.local:8080
```

### Remote unit
We're currently using a remote Raspberry PI 5, accessed via ssh at dirtsim.local.
SSH config is already set up so just:
```bash
ssh dirtsim.local
```

### WebRTC Video Streaming

The HTTP server (port 8080) serves a web dashboard at `/garden` that displays real-time video streams from connected UI instances via WebRTC.

**Architecture:**
- UI application runs headless with LVGL display
- WebRtcStreamer captures frames at 30fps and encodes to H.264 (OpenH264)
- Server creates WebRTC offer and sends to browser via WebSocket signaling
- Browser creates answer and establishes peer connection
- H.264 frames flow via RTP/SRTP, decoded natively by browser

**Access the dashboard:**
```bash
# Local
http://localhost:8080/garden

# Remote (Pi)
http://dirtsim.local:8080/garden
```

**Implementation:**
- **Server**: `src/ui/rendering/WebRtcStreamer.{h,cpp}` - Manages peer connections and H.264 RTP streaming
- **Signaling**: `StreamStart`, `WebRtcAnswer`, `WebRtcCandidate` commands via WebSocket (port 7070)
- **Client**: Native browser RTCPeerConnection (no external libraries needed)
- **Encoding**: OpenH264 at 5Mbps, baseline profile, 30fps

**Key design choice:** Server sends WebRTC offer (not browser) because the sender of media should be the offerer per WebRTC spec.

**Future enhancement - Full remote UI control:**
Currently mouse events (MouseDown/Move/Up) are forwarded from browser to server but only handled in SimRunning state for custom world interaction. To enable full remote control of LVGL widgets (buttons, sliders, toggles in all states), mouse events need to be injected into LVGL's input device (indev) system instead of state machine handlers. This would allow remote interaction with the entire UI including StartMenu, control panels, etc. Implementation would involve creating a custom LVGL indev that reads from remote mouse state.

## References
### Lvgl reference:
If you need to, read the docs here:
https://docs.lvgl.io/master/details/widgets/index.html

### Design docs

Can be found here:
- @design_docs/GridMechanics.md           #<-- Physics system foundations (pressure, friction, cohesion, etc.)
- design_docs/WebRTC-test-driver.md       #<-- Client/Server architecture (DSSM server + UI client)
- design_docs/coding_convention.md        #<-- Code style guidelines
- design_docs/plant.md                    #<-- Tree/organism feature (Phase 1 in progress)
- design_docs/ai-integration-ideas.md     #<-- AI/LLM integration ideas for future

## Project Directory Structure

  dirtsim/
  ├── src/                                   # Source code
  │   ├── core/                              # Shared physics and utilities
  │   │   ├── World.{cpp,h}                  # Grid-based physics simulation
  │   │   ├── Cell.{cpp,h}                   # Pure-material cell implementation
  │   │   ├── WorldEventGenerator.{cpp,h}    # World initialization and particle generation
  │   │   ├── World*Calculator.{cpp,h}       # Physics calculators (8 files)
  │   │   ├── Vector2d.{cpp,h}               # 2D floating point vectors
  │   │   ├── ScenarioConfig.h               # Scenario configuration types
  │   │   ├── WorldData.h                    # Serializable world state
  │   │   └── api/UiUpdateEvent.h            # World update events
  │   ├── server/                            # Headless DSSM server
  │   │   ├── main.cpp                       # Server entry point
  │   │   ├── StateMachine.{cpp,h}           # Server state machine
  │   │   ├── EventProcessor.{cpp,h}         # Event queue processor
  │   │   ├── Event.h                        # Server event definitions
  │   │   ├── states/                        # Server states (Idle, SimRunning, etc.)
  │   │   ├── api/                           # API commands (CellGet, StateGet, etc.)
  │   │   ├── network/                       # WebSocket server and serializers
  │   │   ├── scenarios/                     # Scenario registry and implementations
  │   │   └── tests/                         # Server state machine tests
  │   ├── ui/                                # UI client application
  │   │   ├── main.cpp                       # UI entry point
  │   │   ├── state-machine/                 # UI state machine
  │   │   │   ├── StateMachine.{cpp,h}       # UI state machine
  │   │   │   ├── EventSink.h                # Event routing interface
  │   │   │   ├── states/                    # UI states (Disconnected, SimRunning, etc.)
  │   │   │   ├── api/                       # UI API commands (DrawDebugToggle, etc.)
  │   │   │   └── network/                   # WebSocket client and server
  │   │   ├── rendering/CellRenderer         # World rendering to LVGL
  │   │   ├── controls/ControlPanel          # UI controls (toggles, buttons)
  │   │   ├── ui_builders/LVGLBuilder        # Fluent API for LVGL widgets
  │   │   └── lib/display_backends/          # Wayland, X11, FBDEV backends
  │   ├── cli/                               # Command-line client
  │   │   ├── main.cpp                       # CLI entry point
  │   │   └── WebSocketClient                # CLI WebSocket client
  │   └── tests/                             # Core unit tests
  │       ├── Vector2d_test.cpp              # Math tests
  │       ├── ResultTest.cpp                 # Result<> tests
  │       └── TimersTest.cpp                 # Timer tests
  ├── design_docs/                           # Architecture documentation
  │   ├── GridMechanics.md                   # Physics system design
  │   ├── WebRTC-test-driver.md              # Client/Server architecture
  │   ├── coding_convention.md               # Code style guidelines
  │   └── plant.md                           # Tree/organism feature
  ├── lvgl/                                  # LVGL graphics library (submodule)
  ├── spdlog/                                # Logging library (submodule)
  └── CMakeLists.txt                         # CMake configuration


## Development Status

### Current Focus: Icon-Based UI for HyperPixel 4.0 (In Progress)

**Completed:**
- ✅ IconButtonBuilder & IconRailBuilder fluent API in LVGLBuilder
- ✅ IconRail component (48px wide vertical icon column)
- ✅ ExpandablePanel component (250px slide-out panel)
- ✅ UiComponentManager refactored for icon-based layout
- ✅ SimPlayground panel switching infrastructure
- ✅ Core panel (⚙️) - Quit, stats, debug, render mode controls working
- ✅ Scenario panel (🎬) - Scenario dropdown + sandbox controls working
- ✅ Tree icon (🌳) - Shows/hides based on tree presence, toggles neural grid

**In Progress:**
- Physics panel splitting - Currently General panel shows all PhysicsControls
- Need to extract: PressurePanel, ForcesPanel from PhysicsControls

**New Layout:**
```
Collapsed (default):              Panel Open:
┌───┬─────────────────────┐     ┌───┬────────┬──────────┐
│ ⚙ │                     │     │[⚙]│ Panel  │          │
│ 🎬 │                     │     │ 🎬 │ 250px  │  World   │
│ 🌍 │   World Display     │     │ 🌍 │        │  ~500px  │
│ 💧 │   (~750 x 480)      │     │ 💧 │        │          │
│ ⚡ │                     │     │ ⚡ │        │          │
│ 🌳 │                     │     │ 🌳 │        │          │
└───┴─────────────────────┘     └───┴────────┴──────────┘
```

See `design_docs/icon-rail-ui.md` for full design documentation.

### Tree Organisms (Phase 2 Complete, Phase 3 Next)

**Phase 2 Completed:**
- ✅ ROOT material type (grips soil, can bend)
- ✅ Continuous time system (real deltaTime, all timing in seconds)
- ✅ Contact-based germination (observe dirt 2s → ROOT 2s → WOOD 3s)
- ✅ SEED stays permanent as tree core
- ✅ TreeCommandProcessor (validates energy, adjacency, bounds)
- ✅ Adjacency validation (respects WALL/METAL/WATER boundaries)
- ✅ Balanced growth (maintains ROOT/WOOD/LEAF ratios based on water access)
- ✅ Water-seeking behavior (roots adjust target ratios when water found)
- ✅ LEAF restrictions (air-only, grows from WOOD, cardinal directions)
- ✅ Swap physics integration (organism tracking works with material swaps)
- ✅ UI displays (energy level, current thought)
- ✅ Test coverage (6 passing tests with emoji visualization)

**Known Limitations:**
- No energy regeneration (trees deplete and stop)
- No MATURE stage transition
- **TEMPORARY FIX: LVGL Wayland driver patched** - Modified `lvgl/src/drivers/wayland/lv_wayland.c` to add 100ms timeout to poll() in flush wait callback. This prevents infinite blocking when window is on inactive virtual desktop, but it's a patch to the external LVGL library. Need to find a solution that doesn't require modifying the submodule, or submit patch upstream to LVGL project

**Next Steps:**
- Fix growth topology (extend from tree edges for realistic branching)
- Add basic energy regeneration (LEAFs produce energy over time)
- Phase 3: Resource systems (light ray-casting, photosynthesis, water/nutrient absorption)
- Performance testing and optimization

Awesome Ideas to do soon:
- refactor javascript, async vs promises and let vs var.
- Each process should have it's own logger/log level
- WorldEventGenerator methods should be moved into the Scenarios.
- Add label to tree's view saying which layer it is from.
- Centralize labels on tree's view to one side (top or bottom).
- Implement fragmentation on high energy impacts (see WorldCollisionCalculator).
- Improve some of the scenarios - like the dam break and water equalization ones.
- Fractal world generator?  Or Start from fractal?
- mass as a gravity source!  allan.pizza but in a grid!!!
- quad-tree or quantization or other spatial optimization applied to the grid?
- cli send/receive any command/response automatically.
- ✅ DONE: WebSocketClient library - General-purpose binary protocol client in `src/core/network/`. CLI migrated, all commands support zpp_bits. **Remaining:** UI client still uses old pattern (should migrate), JSON protocol uses snake_case names (binary uses CamelCase - should unify). See design_docs/websocket-client-library.md for status and technical debt notes.
- Review CLI and CLAUDE README/md files for accuracy and gross omission.  Test things
to see if they work.
- Instrument build to figure out which parts take the longest.
- Add light tracing and illumination! (from top down)
- Per-cell neighborhood cache: 64-bit bitmap in each Cell for instant neighbor queries (see design_docs/optimization-ideas.md Section 10).
- Go to http://dirtsim.local and see a monitor page that shows all the dirt sims on the network.  The beginning of a web-based control panel!
- on start menu, q to quit, enter to start?

See design_docs/plant.md and design_docs/ai-integration-ideas.md for details.

### Client/Server Architecture (DSSM + UI Client)
- ✅ Headless server with WebSocket API
- ✅ UI client with controls and rendering
- ✅ Binary serialization (zpp_bits)
- ✅ Per-client format selection with render_format_set API

## Interaction Guidelines
Let me know if you have any questions!
