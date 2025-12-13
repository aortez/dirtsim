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

### Stage 2: Roots ✅
*Headless dirt sim server.*

- [x] `sparkle-duck-server` recipe builds
- [x] systemd service auto-starts server on boot
- [x] WebSocket accessible from network (port 8080)
- [x] CLI tool can control remotely

**Success criteria:** `./cli server StateGet --address ws://dirtsim.local:8080` works from workstation. ✅

### Stage 3: Canopy ✅
*Full graphical UI on display.*

- [x] LVGL rendering via framebuffer (no compositor needed!)
- [x] `sparkle-duck-ui` auto-starts and connects to server
- [x] Disabled getty on tty1 (UI owns the framebuffer)
- [ ] Touch input working (HyperPixel capacitive)
- [ ] WebRTC streaming to garden dashboard

**Success criteria:** Walk up to the Pi, see dirt falling. ✅

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

### Remote Update (A/B System)

The image uses **A/B partitions** for safe remote updates - no more corrupted filesystems! The disk has two rootfs partitions (sda2/sda3). Updates are written to the inactive partition while the system runs from the active one.

```bash
npm run yolo                       # Build + flash to inactive slot + reboot
npm run yolo -- --clean            # Force rebuild (cleans sstate first)
npm run yolo -- --skip-build       # Flash existing image (skip kas build)
npm run yolo -- --hold-my-mead     # Skip confirmation prompt (for scripts)
npm run yolo -- --dry-run          # Show what would happen
```

### Quick Deploy (Userspace Apps)

For fast iteration on application code without rebuilding the full image:

```bash
npm run deploy          # Deploy both server and UI (~20-60s)
npm run deploy server   # Deploy server only
npm run deploy ui       # Deploy UI only
```

This cross-compiles via Yocto, SCPs binaries to the Pi, and restarts services. Much faster than a full YOLO update when you're just changing application code.

**How it works:**
1. Builds the image (unless `--skip-build`)
2. Extracts the rootfs partition from the .wic file
3. Injects your SSH key from `.flash-config.json`
4. Compresses and transfers rootfs to Pi's `/tmp`
5. Verifies checksum
6. Flashes rootfs to the **inactive** partition (safe!)
7. Switches boot flag to use the new partition
8. Reboots and waits for the Pi to come back online

**A/B Management:**
```bash
ssh dirtsim "ab-boot-manager status"        # Show current/inactive slots
ssh dirtsim "sudo ab-boot-manager switch b" # Manually switch to slot b
ssh dirtsim "ab-update /tmp/rootfs.ext4.gz" # Manual A/B update
```

**Rollback:** If an update fails to boot, just switch back to the previous slot and reboot.

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

## Partition Layout

The image uses **A/B partitions** for safe remote updates:

```
/dev/sda1  - boot (150MB, FAT32, shared by both slots)
/dev/sda2  - rootfs_a (800MB, ext4)
/dev/sda3  - rootfs_b (800MB, ext4)
```

At any time, one partition is **active** (currently running) and the other is **inactive** (ready for updates). When you run `npm run yolo`, the system:
1. Writes the new image to the inactive partition
2. Switches the boot flag to use the newly written partition
3. Reboots into the new slot

**Benefits:**
- Updates never corrupt the running system
- Instant rollback by switching boot slots
- No downtime risk during updates

**Check status:**
```bash
ssh dirtsim "ab-boot-manager status"
```

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
│   ├── deploy-apps.mjs       # Quick deploy userspace apps
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
│   ├── recipes-dirtsim/
│   │   └── sparkle-duck/     # Server and UI recipes
│   └── recipes-multimedia/
│       └── libyuv/           # Video encoding dependency
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

### 2025-12-09: Stage 2 & 3 Complete - Server + UI Running!

Both the headless server and LVGL UI are now deployed and running on the Pi:

**What was added:**
- `sparkle-duck-server_git.bb` - Yocto recipe for the headless physics server
- `sparkle-duck-ui_git.bb` - Yocto recipe for the LVGL UI client
- Systemd services for both (server on port 8080, UI on port 7070)
- Disabled `getty@tty1` so UI owns the framebuffer
- Fixed fbdev backend to call `updateAnimations()` (was missing vs wayland backend)
- `npm run deploy` script for quick userspace iteration (~20s vs 3+ min for full YOLO)

**Gotchas:**
- GCC 13 in Yocto is stricter than desktop GCC - had to value-initialize template variables to silence `-Wmaybe-uninitialized` false positives at `-O2`.
- Framebuffer getty overwrites LVGL output - masked `getty@tty1.service` in image recipe.
- LVGL fbdev backend was missing `sm.updateAnimations()` call that wayland backend had - fractal rendered once but didn't animate.

**Architecture:** UI uses LVGL's fbdev backend directly - no Wayland compositor needed! Much simpler than originally planned.

### 2025-12-08: A/B Partitions for Safe Remote Updates

Implemented A/B partition system to fix remote update reliability issues:

**What was added:**
- Custom WKS file (`sdimage-ab.wks`) with dual 800MB rootfs partitions
- `ab-boot-manager` - shell script to manage boot slots (current/inactive/switch/status)
- `ab-update` - wrapper that flashes inactive partition and switches boot
- Modified yolo script to extract rootfs partition instead of full disk image
- BusyBox compatibility: uses `/proc/cmdline` instead of `findmnt`, basic `dd` options

**Result:** Remote updates now work reliably. Tested multiple A↔B update cycles successfully.

### 2025-12-08: HyperPixel 4 Display + HDMI Working

Fixed both displays on Raspberry Pi 5:

**HDMI:** Added `hdmi_force_hotplug=1` to force HDMI output even without EDID detection at boot.

**HyperPixel 4 DPI:**
- Added `fbcon=map:01` to kernel cmdline (maps console to both fb0 and fb1, enables DRM outputs)
- Fixed backlight service to set `bl_power=0` (unblank) in addition to `brightness=1`
- Discovered Pi 5 assigns fb0/fb1 dynamically based on initialization order

**Gotchas:**
- DPI outputs on Pi 5 start `disabled` until something claims them (fbcon, Wayland, etc.)
- HyperPixel backlight is GPIO-based and needs explicit unblanking via `bl_power` sysfs
- `systemctl reboot` works on BusyBox, but `reboot -f` doesn't (symlink to systemctl)

### 2025-12-07: YOLO Remote Update (Original)

Added `npm run yolo` for over-the-network image updates.

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
