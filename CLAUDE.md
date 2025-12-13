# CLAUDE.md

This file provides guidance when working with code in this repository.

## Project Overview

Sparkle Duck is a Dirt-Oriented playground for experimenting with artificial life, Yocto, and LVGL. The centerpiece is a **cell-based multi-material physics simulation** featuring 10 material types (AIR, DIRT, LEAF, METAL, ROOT, SAND, SEED, WALL, WATER, WOOD) with realistic physics including pressure, cohesion, adhesion, friction, and viscosity.

The simulation serves as a substrate for artificial life experiments, currently featuring tree organisms that germinate, grow, and respond to their environment.

## Target Hardware

* Raspberry Pi 4 or 5
* Pi4 display - MPI4008 4" HDMI touchscreen (480x800)
* Pi5 display - HyperPixel 4.0 DPI (480x800)
* Single unified image auto-detects hardware

## Repository Structure

```
sparkle-duck/
├── dirtsim/           # Main simulation application (server, UI, CLI)
├── yocto/             # Yocto layer for building Pi images
└── zephyrproject/     # Zephyr RTOS experiments
```

### dirtsim/ - The Simulation

The main application lives here. It's a client/server architecture:
- **Server**: Headless physics simulation with WebSocket API (port 8080)
- **UI**: LVGL-based display client with controls (port 7070)
- **CLI**: Command-line tool for control, testing, and benchmarking

See `dirtsim/CLAUDE.md` for detailed build instructions, architecture docs, and coding guidelines.

### yocto/ - Pi Image Building

Custom Yocto layer for building bootable images for Raspberry Pi deployment. Includes recipes for the simulation and supporting infrastructure.

## Quick Start

```bash
cd dirtsim
make debug                           # Build
./build-debug/bin/cli run-all        # Run server + UI
./build-debug/bin/cli integration_test  # Quick smoke test
```

## Documentation

| Document | Location | Description |
|----------|----------|-------------|
| Application docs | `dirtsim/CLAUDE.md` | Build, run, test, architecture |
| Physics system | `dirtsim/design_docs/GridMechanics.md` | Pressure, friction, cohesion, etc. |
| Coding conventions | `dirtsim/design_docs/coding_convention.md` | Style guidelines |
| Tree organisms | `dirtsim/design_docs/plant.md` | A-life tree feature |
| CLI reference | `dirtsim/src/cli/README.md` | Command-line interface |
| Yocto deployment | `yocto/` | Yocto layer for building Pi images |

## Remote Deployment

The simulation runs on a Raspberry Pi 4 or 5, accessible at `dirtsim.local` (or custom hostname set during flash):

```bash
# Deploy from workstation (Yocto-based full system)
cd yocto
npm run yolo -- --hold-my-mead             # Build + deploy + reboot
npm run yolo -- --clean-all --hold-my-mead # Full rebuild

# SSH to Pi
ssh dirtsim.local

# Check service
systemctl status sparkle-duck-server
journalctl -u sparkle-duck-server -f

# Verify WebSocket endpoints from workstation
cd dirtsim
./build-debug/bin/cli --address ws://dirtsim.local:8080 server StatusGet  # Server
./build-debug/bin/cli --address ws://dirtsim.local:7070 ui StatusGet      # UI
```

## Git Workflow

- Install hooks: `cd dirtsim && ./hooks/install-hooks.sh`
- Pre-commit runs formatting, linting, and tests.
