# CLAUDE.md - dirtsim

## Quick Reference

- **Build**: `make -C apps debug` or `make -C apps release`
- **Run**: `./apps/build-debug/bin/cli run-all`
- **Test**: `make -C apps test`
- **Format**: `make -C apps format`
- **Deploy**: See `yocto/` for Yocto-based Pi deployment (`npm run yolo`).  Also ./update.sh.

Read:
- README.md
- apps/README.md
- coding_convention.md
- apps/cli/README.md - info on dirtsim-cli
- yocto/README.md - how to deploy

Don't add code to support backwards compatibility unless I tell you to.
If you're going to edit any code, first read coding_convention.md.

## Building
```bash
# Build debug version (outputs to apps/build-debug/).
make -C apps debug

# Build optimized release version (outputs to apps/build-release/).
make -C apps release

# Make the unit tests.
make -C apps build-tests

# Show all available targets.
make -C apps help

# Manual CMake build (if needed).
cmake -B apps/build-debug -S apps -DCMAKE_BUILD_TYPE=Debug
make -C apps/build-debug -j12
```

### Running
```bash
# Run both client and server (debug build).
./apps/build-debug/bin/cli run-all

# Easy clean of up all dirtsim processes.
./apps/build-debug/bin/cli cleanup

# Run benchmark on a remote.
ssh <dirtsim-hostname> "dirtsim-cli benchmark"

# Sending commands (syntax: cli [target] [command] [params]).
./apps/build-debug/bin/cli server StateGet
./apps/build-debug/bin/cli server SimRun '{"timestep": 0.016, "max_steps": 1}'
./apps/build-debug/bin/cli server DiagramGet

# Run everything.
./apps/build-debug/bin/cli run-all
```

### CLI
The CLI is a tool for controlling and communicating with dirtsim.
Use SSH to run it from the same host as you are talking to.\

See: apps/src/cli/README.md

### Debugging and Logging

#### Core Dumps for Crash Analysis

**On the Pi (remote):**
Coredumps are at `/data/coredumps/` (symlinked from `/var/lib/systemd/coredump/`). 
The Pi doesn't have gdb installed, so pull dumps to workstation for analysis.

```bash
# SSH to Pi and list crashes
ssh <dirtsim-hostname> "coredumpctl list -q"

# Get crash details (shows exact filename)
ssh <dirtsim-hostname> "coredumpctl info -q"

# Pull coredump to workstation (use actual filename from info command)
mkdir -p /tmp/crash-analysis
scp <dirtsim-hostname>:/data/coredumps/core.dirtsim-ui.*.zst /tmp/crash-analysis/
```

**Analyzing on workstation:**

Pi crashes produce ARM (aarch64) coredumps. Use the ARM debug binary from Yocto, not the local x86 build.

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
```

**Finding the correct load address:**
The `add-symbol-file` command needs the actual load address from the coredump. Run `info proc mappings` in gdb and look for the dirtsim binary - use the first address shown.

**Local development (workstation crashes):**
```bash
coredumpctl list | grep dirtsim

coredumpctl info

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
- **Server::StateMachine**: Aka DirtSimStateMachine (DSSM). Headless server state machine (Idle ↔ SimRunning ↔ SimPaused → Shutdown)
- **Ui::StateMachine**: UI client state machine (Disconnected → StartMenu → SimRunning ↔ Paused → Shutdown)
- **ScenarioRegistry**: Registry of available scenarios

### UI Framework
- **IconRail**: Vertical icon column for navigation (⚙️ 🎬 🌍 💧 🌳)
- **CellRenderer**: Renders world state to LVGL canvases
- **UiComponentManager**: Manages LVGL screen and containers (icon rail + expandable panel + world)

### Physics Overview
See GridMechanics.md.

## Performance Testing

### Benchmark Tool
The CLI tool includes a benchmark mode for measuring physics performance.
See apps/source/cli/README.md.

## Coding Practices

### Serialization

**Use ReflectSerializer for automatic JSON conversion:**
Do not manually create json objects.  Create C++ objects and convert them to json _only_ when needed.

### Adding a New API Command

To add a new server API command (e.g., `MyCommand`), update these files:

**1. Create the API header and cpp** (`apps/src/server/api/MyCommand.h`):
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

**2. Add to ApiCommand.h** (`apps/src/server/api/ApiCommand.h`):
```cpp
#include "MyCommand.h"  // Add include (alphabetical).

using ApiCommand = std::variant<
    // ...
    Api::MyCommand::Command,  // Add to variant (alphabetical).
    // ...
>;
```

**3. Add to Event.h variant** (`apps/src/server/Event.h`):
```cpp
using ServerEvent = std::variant<
    // ...
    DirtSim::Api::MyCommand::Cwc,  // Add to variant.
    // ...
>;
```

**4. Register handler in StateMachine** (`apps/src/server/StateMachine.cpp`):
```cpp
service.registerHandler<Api::MyCommand::Cwc>(
    [this](Api::MyCommand::Cwc cwc) { queueEvent(cwc); });
```

**5. Add state handler** (e.g., `apps/src/server/states/Idle.cpp`):
```cpp
State::Any Idle::onEvent(const Api::MyCommand::Cwc& cwc, StateMachine& dsm) {
    // Handle command, send response.
    cwc.sendResponse(Api::MyCommand::Response::okay(Api::MyCommand::Okay{}));
    return std::nullopt;  // Stay in current state.
}
```

**6. Register in CLI dispatcher** (`apps/src/cli/CommandDispatcher.cpp`):
```cpp
registerCommand<Api::MyCommand::Command, Api::MyCommand::Okay>(serverHandlers_);
```

After these changes, `./apps/build-debug/bin/cli server MyCommand '{}'` will work.

## Development Environment

### Display Backends
Supports Wayland, X11, SDL, and Linux FBDEV backends.

### Deployment to Raspberry Pi

The project uses Yocto to build complete bootable images for the Pi.

**Deploy from workstation:**
```bash
./update.sh --target <dirtsim-hostname> --fast   # Fast dev deploy (~10s)
cd yocto
npm run yolo -- --hold-my-mead              # Full update + reboot (~2min)
npm run yolo -- --clean-all --hold-my-mead  # Full rebuild + deploy
```

**Check service status on Pi:**
```bash
ssh <dirtsim-hostname>
systemctl status dirtsim-server    # Check status
journalctl -u dirtsim-server -f    # Follow logs
```

**Server details:**
- Runs as system service (starts at boot).
- User: `dirtsim`
- Port: 8080 (WebSocket), 8081 (HTTP dashboard)
- Logs: `/home/dirtsim/dirtsim/dirtsim.log`
- Service file: `/usr/lib/systemd/system/dirtsim-server.service`

See `yocto/` for full Yocto layer documentation and image customization.

### Remote CLI Control

The app communicates with two WebSocket endpoints:
- **Port 8080** (Server): Physics simulation control, world state queries
- **Port 7070** (UI): UI state machine control, display settings

**Check if service is running:**
```bash
ssh <dirtsim-hostname> "dirtsim-cli ui StatusGet"
# Returns: {"state":"StartMenu","connected_to_server":true,"fps":0.0,...}
```

**Important:** To start a simulation remotely, send commands to the **UI** (port 7070), not the server:
```bash
# Start simulation (UI coordinates with server).
ssh <dirtsim-hostname> "dirtsim-cli ui SimRun '{\"scenario_id\": \"sandbox\"}'"

# Query world state (server).
ssh <dirtsim-hostname> "dirtsim-cli server DiagramGet"
ssh <dirtsim-hostname> "dirtsim-cli server StateGet"
```

### Remote unit
We're currently using a remote Raspberry PI 5, accessed via ssh at <dirtsim-hostname>.
SSH config is already set up so just:
```bash
ssh <dirtsim-hostname>
```

### WebRTC Video Streaming

The HTTP server (port 8080) serves a web dashboard at `/garden` that displays real-time video streams from connected UI instances via WebRTC. By default, `/garden` and non-local WebSockets are disabled. Enable LAN access via os-manager to serve `/garden` and accept LAN WebSocket connections with a token. When LAN Web UI is disabled, server/UI WebSocket ports are localhost-only, so remote control must be done over SSH (use `dirtsim-cli` on the device).

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

# Remote (Pi) - requires LAN Web UI enabled and token.
http://<dirtsim-hostname>:8081/garden
```

**Enable LAN Web UI:**
```bash
ssh <dirtsim-hostname> "dirtsim-cli os-manager WebUiAccessSet '{\"enabled\": true}'"
```

The token is shown on the device UI Network panel and in `SystemStatus` as `lan_websocket_token`. Remote WebSocket connections must include `?token=...` (localhost is exempt). LAN Web UI enables incoming WebSocket access automatically.

**Key design choice:** Server sends WebRTC offer (not browser) because the sender of media should be the offerer per WebRTC spec.

**Remote UI control:**
Mouse events (MouseDown/Move/Up) from the browser are injected into LVGL's input device system via `RemoteInputDevice`, enabling remote control of all LVGL widgets in all UI states. The dashboard captures and relays mouse events on the WebRTC video stream.

## References

### Design docs

Can be found here:
- apps/design_docs/evolution-framework.md      #<-- Evolution system architecture
- apps/design_docs/genetic-evolution.md        #<-- Genetic algorithm details
- apps/design_docs/GridMechanics.md            #<-- Physics system foundations
- apps/design_docs/plant.md                    #<-- Tree/organism feature
- apps/design_docs/Architecture.md             #<-- System architecture (server, UI, os-manager)

## Project Directory Structure

  dirtsim/
  ├── apps/
  │   ├── src/
  │   │   ├── core/          # Shared physics, types, and utilities
  │   │   ├── server/        # Headless DSSM server (state machine, API, scenarios)
  │   │   ├── ui/            # UI client (LVGL, controls, rendering, state machine)
  │   │   ├── cli/           # Command-line client
  │   │   └── tests/         # Unit tests
  │   ├── design_docs/       # Architecture and physics documentation
  │   └── CMakeLists.txt
  ├── yocto/                 # Yocto build system for Pi deployment
  ├── coding_convention.md
  └── update.sh

## Git

Never use stash.  Never checkout files unless asked to.  Be careful not to mess up WIP that you might not be aware of.
