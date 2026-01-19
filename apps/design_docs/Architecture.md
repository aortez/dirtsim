# DirtSim Architecture

## System Overview

DirtSim is a multi-process physics simulation stack built for desktop Linux and Raspberry Pi.
It splits physics computation, UI rendering, and privileged system control into separate
processes that communicate over WebSocket APIs.

```
                      HTTP 8081
                +------------------+
                |  Web Dashboard   |
                |  /garden (JS)    |
                +--------+---------+
                         |
                         | JSON WS (8080/7070)
+-------------+ JSON WS +---------------+ binary WS +-------------+
| CLI/Scripts |<------->| UI (LVGL)     |<--------->| Server      |
|             |         | Port 7070     |           | Port 8080   |
+------+------+         +---------------+           +-------------+
       |                             ^                     ^
       | JSON WS (8080)              |                     |
       +-----------------------------+---------------------+
       |
       | JSON WS (9090)
       v
+------------------------+
| OS Manager (privileged |
| service control, reboot|
+------------------------+
```

## Runtime Components

- dirtsim-server: Headless physics simulation, world state, and scenarios.
  - WebSocket on 8080 for binary RenderMessage broadcasts and JSON commands.
  - HTTP server on 8081 serves the dashboard at `/garden`.
- dirtsim-ui: LVGL UI client and control surface.
  - Connects to server for world updates.
  - WebSocket on 7070 for control commands and WebRTC signaling.
- dirtsim-os-manager: Privileged control plane.
  - WebSocket on 9090 for SystemStatus and service control (start/stop/restart/reboot).
  - Manages server/UI services and reports health and system metrics.
- dirtsim-cli: Command-line client for automation, testing, and diagnostics.
- Web dashboard: Browser UI served from `apps/src/server/web/`.
- Systemd units: `dirtsim-server.service`, `dirtsim-ui.service`, `dirtsim-os-manager.service`.

## Ports and Protocols

| Component | Port | Protocol | Purpose |
| --- | --- | --- | --- |
| Server | 8080 | WebSocket (binary + JSON) | Physics data + control commands |
| UI | 7070 | WebSocket (JSON) | UI control + WebRTC signaling |
| OS Manager | 9090 | WebSocket (JSON) | System status + service control |
| Server HTTP | 8081 | HTTP | Web dashboard (`/garden`) |

Binary messages use zpp_bits for high-frequency render updates.
JSON commands are typed at transport boundaries and converted to structs internally.

## State Machines

- Server states: PreStartup, Startup, Idle, SimRunning, SimPaused, Evolution,
  UnsavedTrainingResult, Error, Shutdown.
- UI states: Startup, Disconnected, StartMenu, SimRunning, Paused, Training, Shutdown.
- OS Manager states: Startup, Idle, Error, Rebooting.

Each component uses a dedicated state machine to keep lifecycle transitions explicit
and to keep command handling predictable per state.

## Core Libraries and Subsystems

- core/: Physics, world model, materials, scenarios, and shared utilities.
- core/network/WebSocketService: Unified WebSocket client/server with typed commands.
- ui/rendering/WebRtcStreamer: WebRTC video streaming for browser clients.
- ui/controls/: LVGL control surfaces and panels.
- server/scenarios/: World generators and presets.
- logging-config.json: Centralized log configuration with channel filtering.

## Deployment and Operations

- Yocto builds produce a full Raspberry Pi image with systemd services pre-installed.
- `update.sh` and `npm run yolo` handle builds and deployment.
- OS Manager SystemStatus is used as the post-deploy health gate.
- CLI integration tests restart UI/server before running and reboot afterward.

## Directory Map

```
apps/
  src/
    core/           Shared physics and networking
    server/         Headless simulation server
    ui/             LVGL UI client
    os-manager/     Privileged system controller
    cli/            Command-line client
  design_docs/      Architecture and design notes
yocto/              Raspberry Pi image build and deploy
docker/             CI/local build container
docs/               Supplemental documentation
```
