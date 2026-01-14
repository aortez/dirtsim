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

# Easy clean of up all dirtsim processes.
./build-debug/bin/cli cleanup

# Run benchmark on a remote.
ssh dirtsim2.local "dirtsim-cli benchmark"

# Sending commands (syntax: cli [target] [command] [params]).
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
./build-debug/bin/dirtsim-tests --gtest_filter="StateIdle*"
./build-debug/bin/dirtsim-tests --gtest_filter="StateSimRunning*"

# List all available tests.
./build-debug/bin/dirtsim-tests --gtest_list_tests

# Run specific test.
./build-debug/bin/dirtsim-tests --gtest_filter="StateSimRunningTest.AdvanceSimulation_StepsPhysicsAndDirtFalls"
```

### Debugging and Logging

#### Log Outputs
- **Console**: INFO level and above (colored output)
- **File**: DEBUG/TRACE level and above (`dirtsim.log`)

#### Log Files
- **File**: `dirtsim.log` main application and unit tests
- **Location**: Same directory as executable
- **Behavior**: File is truncated at startup for fresh logs each session

#### Log Levels

**Setting log levels** via `--log-level` flag:
```bash
# Server with debug logging
./build-debug/bin/dirtsim-server --log-level debug -p 8080

# UI with trace logging
./build-debug/bin/dirtsim-ui --log-level trace -b wayland

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

**On the Pi (remote):**
Coredumps are stored on the data partition at `/data/coredumps/` (symlinked from `/var/lib/systemd/coredump/`). The Pi doesn't have gdb installed, so pull dumps to workstation for analysis.

```bash
# SSH to Pi and list crashes
ssh dirtsim.local "coredumpctl list -q"

# Get crash details (shows exact filename)
ssh dirtsim.local "coredumpctl info -q"

# Pull coredump to workstation (use actual filename from info command)
mkdir -p /tmp/crash-analysis
scp dirtsim.local:/data/coredumps/core.dirtsim-ui.*.zst /tmp/crash-analysis/
```

**Analyzing on workstation:**

IMPORTANT: Pi crashes produce ARM (aarch64) coredumps. You MUST use the ARM debug binary from Yocto, not the local x86 build.

```bash
# Install cross-gdb (one time)
sudo apt install gdb-multiarch

# Decompress the dump
cd /tmp/crash-analysis
zstd -d core.dirtsim-ui.*.zst -o core.dump

# Key paths (adjust binary name: dirtsim-ui or dirtsim-server)
YOCTO="/home/data/workspace/dirtsim/yocto/build/tmp"
SYSROOT="$YOCTO/work/raspberrypi_dirtsim-poky-linux/dirtsim-image/1.0/rootfs"
DEBUG_BIN="$YOCTO/work/cortexa72-poky-linux/dirtsim-ui/git/packages-split/dirtsim-ui-dbg/usr/bin/.debug/dirtsim-ui"

# Create GDB command script
cat > gdb-commands.txt << EOF
set sysroot $SYSROOT
file $DEBUG_BIN
core-file core.dump
# Get binary load address from: info proc mappings | grep dirtsim
# Then load symbols at that address (example uses 0x5570780000)
add-symbol-file $DEBUG_BIN 0x5570780000
thread apply all bt
EOF

# Run analysis
gdb-multiarch -batch -x gdb-commands.txt 2>&1 | grep -v "^warning:"

# Or interactive session
gdb-multiarch -x gdb-commands.txt
(gdb) bt                      # Backtrace
(gdb) info threads            # List all threads
(gdb) thread apply all bt     # Backtrace of all threads
(gdb) frame 5                 # Jump to specific frame
(gdb) print variable_name     # Inspect variables
(gdb) info proc mappings      # Show memory mappings (find load address)
(gdb) quit
```

**Finding the correct load address:**
The `add-symbol-file` command needs the actual load address from the coredump. Run `info proc mappings` in gdb and look for the dirtsim binary - use the first address shown.

**Local development (workstation crashes):**
```bash
# List recent core dumps
coredumpctl list | grep dirtsim

# Get info about the latest crash
coredumpctl info

# Analyze directly with gdb (works because x86 binary matches x86 dump)
coredumpctl gdb
```

## Architecture

### Component Libraries

The project is organized into three component libraries:

- **dirtsim-core**: Shared types for serialization (MaterialType, Vector2d/i, Cell, WorldData, ScenarioConfig, RenderMessage)
- **dirtsim-server**: Physics engine (World + calculators), server logic, scenarios, server API commands
- **dirtsim-ui**: UI components (controls, rendering, LVGL builders), UI state machine, UI API commands

Executables (server, UI, CLI, tests) link against these libraries.

### Physics System

- **World**: Grid-based physics simulation with pure-material cells
- **Cell**: Fill ratio [0,1] with single material type per cell
- **MaterialType**: Enum-based material system (AIR, DIRT, LEAF, METAL, SAND, SEED, WALL, WATER, WOOD)
- **Material Properties**: Each material has density, cohesion, adhesion, viscosity, friction, elasticity

### Core Components
- **Vector2d**: 2D floating point vector class
- **Vector2s**: 2D int16 vector class
- **Vector2i**: 2D int32 vector class
- **Server::StateMachine**: Aka DirtSimStateMachine (DSSM). Headless server state machine (Idle ↔ SimRunning ↔ SimPaused → Shutdown)
- **Ui::StateMachine**: UI client state machine (Disconnected → StartMenu → SimRunning ↔ Paused → Shutdown)
- **ScenarioRegistry**: Registry of available scenarios

### UI Framework
- **PanelViewController**: Manages modal view switching within panels
- **ActionButton**: Trough-styled buttons with flexible layouts (all UI buttons)
- **IconRail**: Vertical icon column for navigation (⚙️ 🎬 🌍 💧 🌳)
- **ExpandablePanel**: 250px slide-out panel for controls
- **CoreControls**: Core settings (Quit, Reset, Debug, Render Mode)
- **ScenarioPanel**: Scenario selection with modal navigation
- **PhysicsPanel**: Physics settings organized into sections (General, Pressure, Forces, etc.)
- **CellRenderer**: Renders world state to LVGL canvases
- **NeuralGridRenderer**: Renders 15×15 tree organism perception (side-by-side with world)
- **UiComponentManager**: Manages LVGL screen and containers (icon rail + expandable panel + world)
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

**Enum serialization is automatic.** ReflectSerializer handles enum fields directly using reflect's compile-time enum introspection. Define an enum anywhere, include it in a struct, and it serializes to/from string names automatically:
```cpp
enum class MyEnum : uint8_t { Foo = 0, Bar = 1, Baz = 2 };

struct MyData {
    MyEnum mode = MyEnum::Foo;
};

// Enum serializes as string name automatically.
MyData data{.mode = MyEnum::Bar};
nlohmann::json j = ReflectSerializer::to_json(data);  // {"mode": "Bar"}
MyData data2 = ReflectSerializer::from_json<MyData>(j);
```

Sequential enums starting at 0 work automatically. For non-sequential or non-zero-based enums, specialize `reflect::enum_min`/`reflect::enum_max`.

For C++ APIs, always start with the C++ object, populate it with designated initializers when possible, then convert it as needed in the sending later.

### Adding a New API Command

To add a new server API command (e.g., `MyCommand`), update these files:

**1. Create the API header and cpp** (`src/server/api/MyCommand.h`):
```cpp
#pragma once
#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim::Api::MyCommand {

DEFINE_API_NAME(MyCommand);

struct Command {
    int someField = 0;
    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);
    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool success = true;
    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    using serialize = zpp::bits::members<1>;
};

using Response = Result<Okay, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace DirtSim::Api::MyCommand
```

**2. Add to ApiCommand.h** (`src/server/api/ApiCommand.h`):
```cpp
#include "MyCommand.h"  // Add include (alphabetical).

using ApiCommand = std::variant<
    // ...
    Api::MyCommand::Command,  // Add to variant (alphabetical).
    // ...
>;
```

**3. Add to Event.h variant** (`src/server/Event.h`):
```cpp
using ServerEvent = std::variant<
    // ...
    DirtSim::Api::MyCommand::Cwc,  // Add to variant.
    // ...
>;
```

**4. Register handler in StateMachine** (`src/server/StateMachine.cpp`):
```cpp
service.registerHandler<Api::MyCommand::Cwc>(
    [this](Api::MyCommand::Cwc cwc) { queueEvent(cwc); });
```

**5. Add state handler** (e.g., `src/server/states/Idle.cpp`):
```cpp
State::Any Idle::onEvent(const Api::MyCommand::Cwc& cwc, StateMachine& dsm) {
    // Handle command, send response.
    cwc.sendResponse(Api::MyCommand::Response::okay(Api::MyCommand::Okay{}));
    return std::nullopt;  // Stay in current state.
}
```

**6. Register in CLI dispatcher** (`src/cli/CommandDispatcher.cpp`):
```cpp
registerCommand<Api::MyCommand::Command, Api::MyCommand::Okay>(serverHandlers_);
```

After these changes, `./build-debug/bin/cli server MyCommand '{}'` will work.

## Development Environment

### Display Backends
Supports SDL, Wayland, X11, and Linux FBDEV backends. Primary development is Linux-focused with Wayland backend as default.

### Deployment to Raspberry Pi

The project uses Yocto to build complete bootable images for the Pi.

**Deploy from workstation:**
```bash
cd ../yocto
./update.sh --target dirtsim.local --fast   # Fast dev deploy (~10s)
npm run yolo -- --hold-my-mead              # Full update + reboot (~2min)
npm run yolo -- --clean-all --hold-my-mead  # Full rebuild + deploy
```

**Check service status on Pi:**
```bash
ssh dirtsim.local
systemctl status dirtsim-server    # Check status
journalctl -u dirtsim-server -f    # Follow logs
```

**Server details:**
- Runs as system service (starts at boot).
- User: `dirtsim`
- Port: 8080 (WebSocket), 8081 (HTTP dashboard)
- Logs: `/home/dirtsim/dirtsim/dirtsim.log`
- Service file: `/usr/lib/systemd/system/dirtsim-server.service`

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
http://localhost:8081/garden

# Remote (Pi)
http://dirtsim.local:8081/garden
```

**Implementation:**
- **Server**: `src/ui/rendering/WebRtcStreamer.{h,cpp}` - Manages peer connections and H.264 RTP streaming
- **Signaling**: `StreamStart`, `WebRtcAnswer`, `WebRtcCandidate` commands via WebSocket (port 7070)
- **Client**: Native browser RTCPeerConnection (no external libraries needed)
- **Encoding**: OpenH264 at 5Mbps, baseline profile, 30fps

**Key design choice:** Server sends WebRTC offer (not browser) because the sender of media should be the offerer per WebRTC spec.

**Remote UI control:**
Mouse events (MouseDown/Move/Up) from the browser are injected into LVGL's input device system via `RemoteInputDevice`, enabling remote control of all LVGL widgets (buttons, sliders, toggles) in all UI states. The dashboard captures mouse events on the WebRTC video stream, maps coordinates accounting for letterboxing, and forwards them via WebSocket. `RemoteInputDevice` applies inverse rotation transforms to convert logical (video) coordinates to physical display coordinates before LVGL processes them.

## References
### Lvgl reference:
If you need to, read the docs here:
https://docs.lvgl.io/master/details/widgets/index.html

### Design docs

Can be found here:
- design_docs/coding_convention.md        #<-- Code style guidelines
- design_docs/evolution-framework.md      #<-- Evolution system architecture (states, repository, API)
- design_docs/genetic-evolution.md        #<-- Genetic algorithm details (selection, mutation, fitness)
- @design_docs/GridMechanics.md           #<-- Physics system foundations (pressure, friction, cohesion, etc.)
- design_docs/plant.md                    #<-- Tree/organism feature (Phase 1 in progress)
- design_docs/WebRTC-test-driver.md       #<-- Client/Server architecture (DSSM server + UI client)

## Project Directory Structure

  dirtsim/
  ├── src/                                   # Source code
  │   ├── core/                              # Shared physics and utilities
  │   │   ├── World.{cpp,h}                  # Grid-based physics simulation
  │   │   ├── Cell.{cpp,h}                   # Pure-material cell implementation
  │   │   ├── WorldEventGenerator.{cpp,h}    # World initialization and particle generation
  │   │   ├── World*Calculator.{cpp,h}       # Physics calculators
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
  │   │   ├── PanelViewController.{cpp,h}    # Modal view management
  │   │   ├── rendering/CellRenderer         # World rendering to LVGL
  │   │   ├── controls/                      # UI control panels
  │   │   │   ├── CoreControls               # Core settings panel
  │   │   │   ├── ScenarioPanel              # Scenario selection panel
  │   │   │   ├── PhysicsPanel               # Physics settings panel
  │   │   │   ├── IconRail                   # Icon navigation rail
  │   │   │   └── ExpandablePanel            # Slide-out panel container
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


## Awesome Ideas to do soon
- refactor javascript, async vs promises and let vs var.
- Centralize labels on tree's view to one side (top or bottom).
- Implement fragmentation on high energy impacts (see WorldCollisionCalculator).
- mass as a gravity source!  allan.pizza but in a grid!!!
- reinstall lottie - it would be sweet to mess with these animations. Or sample them for quantized display.
- Add CI test that deploys to a remote unit, then verifies that both the UI and server come up properly.
- tune the color of the icon bar when minimized.  Make it darker.  dim the button some.

See design_docs/plant.md and design_docs/neural-net-brain.md for more.

## Git

Never use stash.  Never checkout files unless asked to.  Be careful not to mess up WIP that you might not be aware of.
