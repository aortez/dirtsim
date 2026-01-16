## Project Overview

DirtSim is a grid/cell-based playground for experimenting with artificial life.

It attempts to simulate both fluid and rigid body dynamics.

It runs on Linux X86 under X11, wayland, and framebuffer.

It also designed as an embedded application, running great on Raspberry Pi 4 and 5.

It also has a very nice clock mode, just hacked in because why not make use of the weird grid physics.

![Dirt Simulation](dirt_sim.png)

## Target Hardware

* Raspberry Pi 4 or 5
* MPI4008 4" HDMI touchscreen (480x800) or HyperPixel 4.0 DPI (480x800)
* Unified image auto-detects hardware
* Also runs natively on linux/x86

## Repository Structure

```
dirtsim/
├── apps/              # Main simulation application (server, UI, CLI)
└── yocto/             # Yocto layer for building Pi images
```

### apps/ - The Simulation

The main application lives here. It's a client/server architecture:
- **Server**: Headless physics simulation with WebSocket API (port 8080)
- **UI**: LVGL-based display client with controls (port 7070)
- **CLI**: Command-line tool for control, testing, and benchmarking

### yocto/ - Pi Image Building

## Quick Start

```bash
cd apps
make debug                           # Build
./build-debug/bin/cli run-all        # Run server + UI
./build-debug/bin/cli integration_test  # Quick smoke test
```

## Documentation

| Document | Location | Description |
|----------|----------|-------------|
| Application docs | `apps/CLAUDE.md` | Build, run, test, architecture |
| Physics system | `apps/design_docs/GridMechanics.md` | Pressure, friction, cohesion, etc. |
| Coding conventions | `apps/design_docs/coding_convention.md` | Style guidelines |
| Tree organisms | `apps/design_docs/plant.md` | A-life tree feature |
| CLI reference | `apps/src/cli/README.md` | Command-line interface |
| Yocto deployment | `yocto/` | Yocto layer for building Pi images |

## Remote Deployment

Accessible at `dirtsim.local` (or custom hostname set during flash):

```bash
# Deploy from workstation (Yocto-based full system)
cd yocto
npm run yolo -- --hold-my-mead             # Build + deploy + reboot
npm run yolo -- --clean-all --hold-my-mead # Full rebuild

# SSH to Pi
ssh dirtsim.local

# Check service
systemctl status dirtsim-server
journalctl -u dirtsim-server -f

# Verify WebSocket endpoints from workstation
cd apps
./build-debug/bin/cli --address ws://dirtsim.local:8080 server StatusGet  # Server
./build-debug/bin/cli --address ws://dirtsim.local:7070 ui StatusGet      # UI
```

## Git Workflow

- Install hooks: `cd apps && ./hooks/install-hooks.sh`
- Pre-commit runs formatting, linting, and tests.
