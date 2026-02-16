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
├── apps/              # Main simulation application
│   ├── src/server/    # Headless physics simulation (WebSocket API :8080)
│   ├── src/ui/        # LVGL display client (WebSocket API :7070)
│   └── src/cli/       # Command-line tool for control, testing, benchmarking
└── yocto/             # Yocto layer for building Pi images
```

## Quick Start

```bash
cd apps
make debug                           # Build
./build-debug/bin/cli run-all        # Run server + UI
./build-debug/bin/cli functional-test canTrain  # Smoke test (requires running UI/server)
```

## Documentation

| Document | Location | Description |
|----------|----------|-------------|
| Application docs | `apps/CLAUDE.md` | Build, run, test, architecture |
| Architecture | `apps/design_docs/Architecture.md` | System overview and components |
| Physics system | `apps/design_docs/GridMechanics.md` | Pressure, friction, cohesion, etc. |
| Coding conventions | `apps/design_docs/coding_convention.md` | Style guidelines |
| Tree organisms | `apps/design_docs/plant.md` | A-life tree feature |
| CLI reference | `apps/src/cli/README.md` | Command-line interface |
| Yocto deployment | `yocto/` | Yocto layer for building Pi images |

## Host Roles

- `garden.local`: Dedicated x86 long-term training box. This is the operational default trainer host.
- `dirtsim.local`: Default single-device host (usually a Pi).
- `dirtsim2.local`: Common secondary test device.

Any host can run training, but team workflows treat `garden.local` as the canonical trainer.

## Remote Deployment

Use `dirtsim.local` by default (or custom hostname set during flash). For trainer workflows, use
`garden.local` with the same commands via `--target` or `--address`.

```bash
# Deploy from workstation (Yocto-based full system)
cd yocto
npm run yolo -- --hold-my-mead             # Build + deploy + reboot
npm run yolo -- --clean-all --hold-my-mead # Full rebuild
npm run yolo -- --target garden.local --fast --hold-my-mead # Fast deploy to trainer

# SSH to Pi
ssh dirtsim.local

# Check service
systemctl status dirtsim-server
journalctl -u dirtsim-server -f

# Verify Pi WebSocket endpoints from workstation
cd apps
./build-debug/bin/cli --address ws://dirtsim.local:8080 server StatusGet  # Server
./build-debug/bin/cli --address ws://dirtsim.local:7070 ui StatusGet      # UI

# Verify trainer status (garden.local server usually binds localhost only)
ssh garden.local "dirtsim-cli server StatusGet"
ssh garden.local "dirtsim-cli ui StatusGet"
ssh garden.local "sudo journalctl -u dirtsim-server.service --since '5 min ago' --no-pager | grep -E 'Evolution: gen=|Generation [0-9]+ complete' | tail -n 20"

# Optional: tunnel trainer server to use local workstation CLI commands
ssh -N -L 28080:localhost:8080 garden.local
./build-debug/bin/cli --address ws://localhost:28080 server TrainingResultList
./build-debug/bin/cli --address ws://localhost:28080 server TrainingResultGet '{"trainingSessionId":"<uuid>"}'
```

## Git Workflow

- Install hooks: `cd apps && ./hooks/install-hooks.sh`
- Pre-commit runs formatting, linting, and tests.
