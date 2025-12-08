# Sparkle Duck Yocto Build

A custom Yocto Linux image for running the Sparkle Duck dirt simulation on Raspberry Pi 5.

## Goals

Build a minimal, purpose-built Linux image that:
- Boots fast and runs lean
- Contains only what we need for the dirt sim
- Can be updated atomically (A/B partitions via Mender)
- Eliminates the cruft of a general-purpose distro

## Development Stages

### Stage 1: Bare Earth ✅
*Minimal bootable image with remote access.*

- [x] Basic Yocto build environment set up (kas + scarthgap)
- [x] Boots on Pi 5 (from USB drive)
- [x] SSH access working (socket-activated)
- [x] Network configured (DHCP via systemd-networkd)
- [x] mDNS working (avahi - accessible as dirtsim.local)
- [x] Persistent journald logs for debugging
- [x] Basic utils: bash, systemd, htop, nano

**Success criteria:** SSH into `dirtsim.local`, poke around, reboot, it comes back. ✅

1.5 Utils!
- nmon, vim, anything else obvious that we'll want for remote access/automation?

1.6 HDMI LCD (for troubleshooting/dev purposes)!

1.7 The actual target display!
https://github.com/pimoroni/hyperpixel4

### Stage 2: Roots
*Headless dirt sim server.*

- [ ] `sparkle-duck-server` recipe builds
- [ ] systemd service auto-starts server on boot
- [ ] WebSocket accessible from network (port 8080)
- [ ] CLI tool can control remotely

**Success criteria:** `./cli server StateGet --address ws://dirtsim.local:8080` works from workstation.

### Stage 3: Canopy
*Full graphical UI with streaming.*

- [ ] Wayland compositor (cage for kiosk mode)
- [ ] LVGL rendering via DRM/OpenGL ES
- [ ] `sparkle-duck-ui` auto-starts
- [ ] WebRTC streaming to garden dashboard

**Success criteria:** Walk up to the Pi, see dirt falling. Open browser, see the stream.

---

## Directory Structure

```
yocto/
├── README.md                 # This file
├── meta-dirtsim/             # Our custom Yocto layer
│   ├── conf/
│   │   └── layer.conf
│   ├── recipes-core/
│   │   └── images/
│   │       └── dirtsim-image.bb   # Our custom image recipe
│   ├── recipes-connectivity/      # Network customizations
│   └── recipes-dirtsim/           # Our application binaries (future)
├── kas-dirtsim.yml           # KAS build configuration
└── build/                    # Build output (gitignored)
```

## Yocto Version

Using **Scarthgap (5.0)** - confirmed working on Raspberry Pi 5 with full A/B boot support.

### Required Layers

| Layer | Purpose |
|-------|---------|
| poky | Core Yocto (bitbake, oe-core) |
| meta-raspberrypi | Pi 5 BSP |
| meta-openembedded | Additional recipes (NetworkManager, etc.) |
| meta-dirtsim | Our custom layer and image |
| meta-mender-community | OTA updates (optional, Stage 2+) |

---

## Working Notes

*This section captures discoveries and decisions as we go. Will be pruned later.*

### 2025-12-07: Stage 1 Complete!

Fixed two critical issues that were blocking SSH:

1. **Volatile /var/log** - Yocto's default base-files makes `/var/log` a symlink to tmpfs, so logs vanish on reboot and journald can't persist. Fixed with `VOLATILE_LOG_DIR = "no"` in base-files bbappend plus a journald drop-in for `Storage=persistent`.

2. **Wrong boot partition in fstab** - Default fstab expects `/dev/mmcblk0p1` for boot (SD card), but we boot from USB (`/dev/sda1`). Systemd waited 90 seconds for the SD card, then `local-fs.target` failed, which cascaded to prevent sshd.socket from starting. Fixed with custom fstab pointing to `/dev/sda1`.

Lesson: When systemd says "connection refused", check dependency failures with `journalctl`. The logs tell the whole story.

### 2025-12-06: Initial Planning

Reference for Pi 5 Yocto support: https://hub.mender.io/t/raspberry-pi-5/6689

Key insight from Mender docs: On Pi 5, kernel and device trees live on A/B partitions, enabling full system swaps including kernel updates.

Decision: Start with Stage 1 (bare earth) before adding our application. Get the foundation solid first.

---

## Getting Started

*TODO: Fill in as we work through Stage 1*

### Prerequisites (

First, use test-lvgl/scripts/setup_dep script (TODO improve this note).

```bash
# KAS build tool
pip3 install kas
```

#### Ubuntu 24.04: AppArmor User Namespace Fix

Ubuntu 24.04 restricts unprivileged user namespaces by default, which breaks
bitbake's network isolation during builds. You'll see errors like:

```
PermissionError: [Errno 1] Operation not permitted
  File "/proc/self/uid_map", "w"
```

**Temporary fix** (resets on reboot):
```bash
sudo sysctl kernel.apparmor_restrict_unprivileged_userns=0
```

**Permanent fix** - create `/etc/sysctl.d/99-yocto-userns.conf`:
```
kernel.apparmor_restrict_unprivileged_userns = 0
```

Then apply with `sudo sysctl --system`.

### Building

```bash
cd yocto
kas build kas-dirtsim.yml
```

First build takes 2-4 hours. Subsequent builds are much faster thanks to sstate cache.

The output image will be at:
```
build/tmp/deploy/images/raspberrypi5/dirtsim-image-raspberrypi5.rootfs.wic.gz
```

### Flashing

```bash
npm run flash                       # Interactive device selection
npm run flash -- --device /dev/sdb  # Direct flash (still confirms)
npm run flash -- --list             # Just list available devices
npm run flash -- --dry-run          # Show what would happen
```

---

## What We're Pruning Away

Compared to Raspberry Pi OS, our image will NOT include:

- Desktop environment (PIXEL, etc.)
- Package manager (apt, dpkg, dpkg database)
- X11 (pure Wayland)
- Python, Perl, Ruby
- Documentation, man pages
- Bluetooth stack (unless we need it later)
- Audio stack (unless we need it later)
- Most /usr/share locale data

## What We're Keeping

- systemd (service management, journald)
- Wayland (stage 3)
- OpenGL ES / DRM (stage 3)
- SSH server
- Network stack
- Our dirt sim binaries
