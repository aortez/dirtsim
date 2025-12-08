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
- [x] SSH access working (key-based, socket-activated)
- [x] Network configured (DHCP via NetworkManager)
- [x] mDNS working (avahi - accessible as dirtsim.local)
- [x] Persistent journald logs for debugging
- [x] Basic utils: bash, systemd, htop, nano, vim, nmon, curl, jq, etc.
- [x] Security hardened (no passwords, SSH keys only, root disabled)

**Success criteria:** `ssh dirtsim` connects, poke around, reboot, it comes back. ✅

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

## Getting Started

### Prerequisites

Install the KAS build tool:
```bash
pip3 install kas
```

On Ubuntu 24.04, you'll also need to allow unprivileged user namespaces for bitbake:
```bash
# Permanent fix - create this file:
echo 'kernel.apparmor_restrict_unprivileged_userns = 0' | sudo tee /etc/sysctl.d/99-yocto-userns.conf
sudo sysctl --system
```

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

The flash tool writes the image and injects your SSH public key for passwordless login.

```bash
npm run flash                       # Interactive device selection
npm run flash -- --device /dev/sdb  # Direct flash (still confirms)
npm run flash -- --list             # Just list available devices
npm run flash -- --dry-run          # Show what would happen
npm run flash -- --reconfigure      # Re-select SSH key
```

**First-time setup:** The script prompts you to select an SSH public key from `~/.ssh/`. Your choice is saved to `.flash-config.json` (gitignored) so subsequent flashes are faster.

### Remote Update (YOLO Mode)

Once you have a working Pi with SSH access, you can push updates over the network without physically swapping the disk:

```bash
npm run yolo                       # Build + prepare + flash + reboot
npm run yolo -- --skip-build       # Flash existing image (skip kas build)
npm run yolo -- --hold-my-mead     # Skip confirmation prompt (for scripts)
npm run yolo -- --dry-run          # Show what would happen
```

**How it works:**
1. Builds the image (unless `--skip-build`)
2. Decompresses and mounts the image locally
3. Injects your SSH key from `.flash-config.json`
4. Recompresses the customized image
5. SCPs to the Pi's `/tmp`
6. Verifies checksum
7. Runs `dd` to flash the running system's disk
8. Reboots and waits for the Pi to come back online

**Warning:** This overwrites the boot disk while the system is running. If something goes wrong, you'll need to pull the disk and reflash via `npm run flash`. Hence "YOLO mode."

**Requirements:**
- Pi must be accessible via SSH at `dirtsim.local`
- Workstation needs `sudo` access for loop device mounting
- SSH key must be configured (run `npm run flash -- --reconfigure` if needed)

### Connecting

After flashing, boot the Pi and connect:
```bash
ssh dirtsim@dirtsim.local
```

Or add this to your `~/.ssh/config` for a shorter command:
```
Host dirtsim
    HostName dirtsim.local
    User dirtsim
    IdentityFile ~/.ssh/id_ed25519_sparkle_duck
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
```

Then just: `ssh dirtsim`

---

## Security Model

The image uses **SSH key authentication only** - no passwords anywhere:

- **User:** `dirtsim` (with passwordless sudo)
- **Root:** Disabled for SSH login
- **Authentication:** SSH public key only (injected at flash time)
- **Password auth:** Disabled in sshd

This is more secure than password auth and more convenient (no password to remember).

**To use a different SSH key:**
```bash
npm run flash -- --reconfigure
```

**If you don't have an SSH key:**
```bash
ssh-keygen -t ed25519
```

---

## Directory Structure

```
yocto/
├── README.md                 # This file
├── kas-dirtsim.yml           # KAS build configuration
├── package.json              # npm scripts for flash/update
├── scripts/
│   ├── flash.mjs             # Flash image + inject SSH key
│   ├── update.mjs            # Build + flash + verify (sneakernet)
│   └── yolo-update.mjs       # Remote update over network
├── meta-dirtsim/             # Our custom Yocto layer
│   ├── conf/
│   │   └── layer.conf
│   ├── recipes-core/
│   │   ├── base-files/       # fstab, profile customizations
│   │   ├── images/
│   │   │   └── dirtsim-image.bb
│   │   └── systemd/          # journald persistence
│   ├── recipes-connectivity/
│   │   ├── networkmanager/   # NM configuration
│   │   └── openssh/          # SSH hardening
│   ├── recipes-extended/
│   │   └── sudo/             # Passwordless sudo for dirtsim
│   └── recipes-dirtsim/      # Our application (future)
└── build/                    # Build output (gitignored)
```

## Yocto Version

Using **Scarthgap (5.0)** - confirmed working on Raspberry Pi 5.

### Layers

| Layer | Purpose |
|-------|---------|
| poky | Core Yocto (bitbake, oe-core) |
| meta-raspberrypi | Pi 5 BSP |
| meta-openembedded | Additional recipes (NetworkManager, etc.) |
| meta-dirtsim | Our custom layer and image |

---

## What's Included

- systemd (service management, journald)
- NetworkManager (WiFi, nmtui/nmcli)
- OpenSSH server (key-only)
- avahi (mDNS - .local hostname)
- Development tools: vim, htop, nmon, curl, jq, strace, screen, rsync

## What's Pruned

Compared to Raspberry Pi OS, our image does NOT include:

- Desktop environment
- Package manager (apt, dpkg)
- X11 (pure Wayland when we add graphics)
- Python, Perl, Ruby
- Documentation, man pages
- Bluetooth stack
- Most /usr/share locale data

---

## Working Notes

### 2025-12-07: YOLO Remote Update

Added `npm run yolo` for over-the-network image updates without physically swapping the disk:

- Prepares image locally (decompresses, mounts via loop device, injects SSH key)
- SCPs customized image to Pi's `/tmp`
- Verifies checksum before flashing
- Runs `dd` on the live system and reboots
- BusyBox compatibility: uses basic `dd` options (no `status=progress` or `conv=fsync`)

**Gotchas encountered:**
- BusyBox `dd` doesn't support GNU options - had to simplify the command.
- BusyBox `df` doesn't support `-B1` flag - use `-k` and multiply by 1024.
- Must inject SSH key into image or you'll be locked out after flash!

### 2025-12-07: Security Hardening

Removed `debug-tweaks` (passwordless root) and implemented proper SSH key authentication:

- Created `dirtsim` user with passwordless sudo.
- Disabled root SSH login entirely.
- Disabled password authentication (SSH keys only).
- Flash script prompts for SSH key selection and injects at flash time.
- User's key preference saved to `.flash-config.json` (gitignored).
- **Gotcha:** Must unlock the account with `usermod -p '*'` or SSH rejects keys.

### 2025-12-07: Stage 1 Complete!

Fixed two critical issues that were blocking SSH:

1. **Volatile /var/log** - Yocto's default makes `/var/log` a tmpfs symlink. Fixed with `VOLATILE_LOG_DIR = "no"` plus journald `Storage=persistent`.

2. **Wrong boot partition in fstab** - Default expects SD card (`/dev/mmcblk0p1`), we boot from USB (`/dev/sda1`). Systemd waited 90 seconds then failed. Fixed with custom fstab.

Lesson: When systemd says "connection refused", check `journalctl` for dependency failures.

### 2025-12-06: Initial Planning

Reference for Pi 5 Yocto support: https://hub.mender.io/t/raspberry-pi-5/6689

Decision: Start with Stage 1 (bare earth) before adding our application. Get the foundation solid first.
