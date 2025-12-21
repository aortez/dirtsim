# DirtSim Architecture

## System Overview

DirtSim is a **client-server physics simulation** with interactive UI and web-based monitoring. The architecture separates physics computation (headless server) from visualization (UI client) for performance and flexibility.

```
┌─────────────────┐         ┌──────────────────┐         ┌─────────────┐
│  Browser        │         │   UI Client      │         │   Server    │
│  Dashboard      │◄─JSON──►│   (LVGL)         │◄─Binary─►│   (DSSM)    │
│  (JavaScript)   │         │                  │         │  Headless   │
│                 │         │  Port 7070       │         │  Port 8080  │
│  • Monitor      │         │  • Rendering     │         │  • Physics  │
│  • Control      │         │  • Controls      │         │  • World    │
│  • WebRTC video │         │  • WebRTC stream │         │  • Broadcast│
└─────────────────┘         └──────────────────┘         └─────────────┘
```

## Communication Patterns

### Server (DSSM) - Port 8080

**Role:** Headless physics engine broadcasting world state.

**Binary protocol:** High-frequency world updates to UI clients
- RenderMessage broadcasts at simulation FPS
- small, fast, zpp_bits serialized

**JSON protocol:** CLI commands and external tools
- StateGet, SimRun, CellSet, etc.
- Human-readable, debuggable
- Used by `./build-debug/bin/cli server <command>`

### UI Client - Port 7070

**Dual role:** Client to server + server for external control.

**As client (to server):**
- Connects to DSSM on port 8080
- Receives RenderMessage broadcasts (binary)
- Sends commands (SimRun, CellSet, etc.)

**As server (from browser/CLI):**
- Listens on port 7070
- Accepts control commands (StatusGet, MouseDown, etc.)
- Streams video via WebRTC

### Browser Dashboard

**Connects to:** UI port 7070 (JSON), Server port 8080 (JSON)

**Features:**
- Real-time status monitoring
- WebRTC video streaming (H.264, 30fps)
- Mouse event forwarding to LVGL UI
- Remote control (SimRun, Exit, etc.)

**WebRTC signaling:**
- Synchronous SDP offer in StreamStart response
- Trickle ICE candidates via sendToClient()
- Connection setup: ~20ms

### CLI Tool

**Pure client:** Sends JSON commands, displays responses.

```bash
# Server commands
./build-debug/bin/cli server StateGet
./build-debug/bin/cli server SimRun '{"timestep": 0.016, "max_steps": 1000}'

# UI commands
./build-debug/bin/cli ui StatusGet --address ws://localhost:7070
./build-debug/bin/cli ui Exit

# Quick smoke test
./build-debug/bin/cli integration_test
```

## Core Components

### WebSocketService

Unified client+server WebSocket library with type-safe command handlers.

**Location:** `src/core/network/` (see `README.md` for full design)

**Key features:**
- Dual-role (client + server in one instance)
- Binary (zpp_bits) + JSON protocol support
- One-line handler registration
- Directed messaging via connection IDs

### State Machines

**Server (DSSM):** `Idle` → `SimRunning` ↔ `SimPaused` → `Shutdown`
- Owns World, advances physics, broadcasts updates
- Stateless API handlers (StateGet, StatusGet, etc.)

**UI:** `Disconnected` → `StartMenu` → `SimRunning` ↔ `Paused` → `Shutdown`
- Renders world, handles input, streams video
- Connects to DSSM server for world data

### Physics System

**World:** Grid-based pure-material simulation (9 material types, fill ratios)

**Location:** `src/core/` - shared by both server and UI for local simulation

**Calculators:** Stateless physics (pressure, cohesion, friction, etc.) - see `design_docs/GridMechanics.md`

### WebRTC Streaming

**WebRtcStreamer:** H.264 video encoding and RTP transmission.

**Flow:**
1. Browser sends `StreamStart` with clientId
2. UI creates peer connection, returns SDP offer synchronously
3. ICE candidates trickle via `sendToClient(connectionId, candidate)`
4. Browser sends answer via `WebRtcAnswer`
5. Connection established, frames flow at 30fps

**Encoding:** OpenH264 at 5Mbps, baseline profile, ~33ms frame interval.

## Directory Structure

```
src/
├── core/                      # Shared: physics, networking, types
│   ├── World.{h,cpp}         # Physics grid simulation
│   ├── network/              # WebSocketService + BinaryProtocol
│   └── CommandWithCallback.h # Async command pattern
│
├── server/                    # DSSM headless server
│   ├── StateMachine          # Server state machine
│   ├── states/               # Idle, SimRunning, SimPaused, Shutdown
│   ├── api/                  # API commands (20+ commands)
│   ├── network/              # HttpServer, PeerDiscovery
│   └── scenarios/            # World generators
│
├── ui/                        # LVGL UI client
│   ├── state-machine/        # UI state machine
│   │   ├── states/          # Disconnected, StartMenu, SimRunning, etc.
│   │   ├── api/             # UI API commands (15 commands)
│   │   └── network/         # CommandDeserializerJson
│   ├── rendering/           # CellRenderer, WebRtcStreamer
│   ├── controls/            # PhysicsControls, SandboxControls
│   └── lib/                 # Display backends (Wayland, X11, FBDEV)
│
└── cli/                      # Command-line client
    ├── CommandDispatcher    # Type-safe command routing
    └── BenchmarkRunner      # Performance testing
```

## Quick Reference

**Start everything:**
```bash
./build-debug/bin/cli run-all              # Server + UI together
```

**Access points:**
- DSSM server: `ws://localhost:8080`
- UI control: `ws://localhost:7070`
- Web dashboard: `http://localhost:8081/garden`

**Remote Pi:**
- SSH: `ssh dirtsim.local`
- WebSockets: `ws://dirtsim.local:8080`, `ws://dirtsim.local:7070`
- Dashboard: `http://dirtsim.local:8081/garden`

**Testing:**
```bash
make test                            # Unit tests
./build-debug/bin/cli integration_test    # End-to-end smoke test
./build-release/bin/cli benchmark   # Performance metrics
```

## Design Philosophy

**Separation of concerns:** Physics and rendering are independent processes. Server runs headless, UI connects as needed.

**Type safety:** All API commands are strongly typed. Compiler catches errors before runtime.

**Protocol efficiency:** Binary for high-frequency data, JSON for human/browser interaction.

**Minimal coupling:** Core physics has no UI dependencies. State machines coordinate without direct references.

## Related Documents

- `src/core/network/README.md` - WebSocketService design details
- `design_docs/GridMechanics.md` - Physics system specification
- `design_docs/plant.md` - Tree organism feature
- `design_docs/coding_convention.md` - Code style guide
