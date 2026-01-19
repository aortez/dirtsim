# DirtSim - Physics Simulation

A cell-based multi-material physics simulation with interactive UI, demonstrating advanced physics simulation using Yocto, Zephyr, and LVGL technologies.

## Features

- **Pure-material physics system** with fill ratios and 9 material types (AIR, DIRT, LEAF, METAL, SAND, SEED, WALL, WATER, WOOD)
- **Client/Server Architecture** - Headless physics server + UI client
- **OS Manager** - Privileged service control and system status
- **Multiple Display Backends** - X11, Wayland, FBDEV support
- **Tree Organisms** - Germination, growth, and perception systems
- **WebSocket API** - Remote control and integration testing

## Quick Start

### 1. Install Dependencies

#### Ubuntu/Debian
```bash
# Run the automated setup script
./scripts/setup_dependencies.sh

# Or install manually:
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libboost-dev \
    libssl-dev \
    libx11-dev \
    libwayland-dev \
    libwayland-client0 \
    libwayland-cursor0 \
    libxkbcommon-dev \
    libnm-dev \
    wayland-protocols \
    clang-format
```

#### Arch Linux
```bash
sudo pacman -S base-devel cmake pkgconf boost openssl libx11 wayland wayland-protocols xkbcommon clang networkmanager
```

#### Fedora/RHEL
```bash
sudo dnf install gcc-c++ cmake pkgconfig boost-devel openssl-devel libX11-devel wayland-devel wayland-protocols-devel libxkbcommon-devel NetworkManager-libnm-devel clang-tools-extra
```

### 2. Initialize Git Submodules

The project uses git submodules for LVGL and other dependencies:
```bash
# Initialize and update all submodules
git submodule update --init --recursive
```

### 3. Build

```bash
# Debug build (recommended for development)
make debug

# Or release build (optimized)
make release
```

### 4. Run

#### Server + UI Together
```bash
# Terminal 1: Start headless server
./build-debug/bin/dirtsim-server -p 8080

# Terminal 2: Start UI client
./build-debug/bin/dirtsim-ui -b wayland --connect localhost:8080
```

#### Standalone UI (built-in physics)
```bash
# Wayland backend (default)
./build-debug/bin/dirtsim-ui -b wayland

# X11 backend
./build-debug/bin/dirtsim-ui -b x11

# Custom window size
./build-debug/bin/dirtsim-ui -b x11 -W 1200 -H 1200
```

## Display Backend Support

The project supports multiple display backends:

- **Wayland** - Default, modern Linux compositor protocol
- **X11** - Traditional X Window System
- **FBDEV** - Linux framebuffer device (for embedded systems)

Backend selection is done at runtime via the `-b` flag.

## Testing

```bash
# Run all unit tests
make test

# Run specific test
make test ARGS='--gtest_filter=StateIdle*'

# Run with AddressSanitizer (memory error detection)
make test-asan
```

## CLI Tool

The CLI tool provides remote control, testing, and automation capabilities:

```bash
# Launch server + UI together
./build-debug/bin/cli run-all

# Send commands to server or UI
./build-debug/bin/cli server StatusGet
./build-debug/bin/cli server SimRun '{"scenario_id": "sandbox"}'
./build-debug/bin/cli ui StatusGet

# Take a screenshot
./build-debug/bin/cli screenshot output.png

# Remote control (Raspberry Pi)
./build-debug/bin/cli --address ws://dirtsim.local:8080 server StatusGet
./build-debug/bin/cli screenshot --address ws://dirtsim.local:7070 remote.png

# Clean up all dirtsim processes
./build-debug/bin/cli cleanup
```

**See [src/cli/README.md](src/cli/README.md) for complete documentation.**

## Development

### Code Formatting
```bash
make format
```

### Available Make Targets
```bash
make help
```

### Architecture Overview

See [design_docs/Architecture.md](design_docs/Architecture.md) for the system overview and
[CLAUDE.md](CLAUDE.md) for detailed project documentation, including:
- Component libraries (core, server, UI)
- Physics system (World, Cell, Materials)
- State machines (Server DSSM, UI client)
- WebSocket API
- Coding conventions

## Troubleshooting

### Build fails with "cmake: command not found"
Install build dependencies (see section 1 above)

### X11 backend fails to start
```bash
# Check X11 libraries are installed
pkg-config --exists x11 && echo "X11 found" || echo "X11 missing"

# Install if missing
sudo apt-get install libx11-dev
```

### Wayland backend fails
```bash
# Ensure Wayland libraries are present
pkg-config --exists wayland-client && echo "Wayland found" || echo "Wayland missing"
```

### Display backend not enabled
Check `lv_conf.h` for backend configuration:
```bash
grep "^#define LV_USE_" lv_conf.h | grep -E "(SDL|X11|WAYLAND|FBDEV)"
```

## Performance Testing

```bash
# Basic benchmark (headless server, 120 steps)
./build-debug/bin/cli benchmark --steps 120

# Simulate UI client load
./build-debug/bin/cli benchmark --steps 120 --simulate-ui
```

## Project Structure

```
dirtsim/
├── src/
│   ├── core/          # Shared physics and utilities
│   ├── server/        # Headless DSSM server
│   ├── ui/            # UI client application
│   ├── cli/           # Command-line client
│   └── tests/         # Unit tests
├── design_docs/       # Architecture documentation
├── scripts/           # Build and setup scripts
├── CLAUDE.md          # Detailed project documentation
└── CMakeLists.txt     # CMake build configuration
```

## Contributing

Please follow the coding conventions in [design_docs/coding_convention.md](design_docs/coding_convention.md).

Run `make format` before committing.

## License

See individual source file headers for licensing information.
