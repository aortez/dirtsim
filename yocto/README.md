# DirtSim Yocto Build

A custom Yocto Linux image for running the DirtSim dirt simulation on Raspberry Pi 4 and Pi 5.

**Hardware Support:**
- **Pi4:** MPI4008 4" HDMI touchscreen (480x800, resistive touch via ADS7846)
- **Pi5:** HyperPixel 4.0 DPI display (480x800, capacitive touch via Goodix)
- Single unified image auto-detects hardware and configures display/touch appropriately

## Goals

Build a minimal, purpose-built Linux image that:
- Boots fast and runs lean
- Contains only what we need for the dirt sim
- Can be updated atomically (A/B partitions)
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

- [x] `dirtsim-server` recipe builds
- [x] systemd service auto-starts server on boot
- [x] WebSocket accessible from network (port 8080)
- [x] CLI tool can control remotely

**Success criteria:** `./cli server StateGet --address ws://dirtsim.local:8080` works from workstation. ✅

### Stage 3: Canopy ✅
*Full graphical UI on display.*

- [x] LVGL rendering via framebuffer (no compositor needed!)
- [x] `dirtsim-ui` auto-starts and connects to server
- [x] Disabled getty on tty1 (UI owns the framebuffer)
- [x] Touch input working (both Pi4 ADS7846 and Pi5 Goodix)
- [x] Auto-detect Pi model and configure display rotation/touch device
- [ ] WebRTC streaming to garden dashboard

**Success criteria:** Walk up to the Pi, see dirt falling, touch works. ✅

### Recent Improvements (Dec 2025)

**WiFi Provisioning via BLE:**
- [x] Integrated `wifi-provisioner` daemon (Improv WiFi protocol over BLE)
- [x] Auto-advertises on boot when WiFi disconnected
- [x] Web Bluetooth from phone → provision credentials → Pi connects
- [x] Tested end-to-end on Pi 5

**Image Refactoring:**
- [x] dirtsim-image now inherits pi-base-image (-71 lines, removed duplication)
- [x] Bluetooth support (bluez5) added to base image
- [x] systemd-networkd disabled (fixes 120s boot delay)
- [x] Automatic inclusion of shared infrastructure (A/B boot, persistent data, etc.)

**API Improvements:**
- [x] Removed scenario_id from SimRun command (server uses configured scenario)
- [x] Added ScenarioSwitch command (switch scenarios at runtime)
- [x] Added ScenariosListGet command (UI requests from server dynamically)
- [x] Removed static SCENARIO_METADATA duplication (single source of truth)
- [x] Removed obsolete server.json runtime config path

**Tooling:**
- [x] Added `tail_remote_logs.sh` for remote debugging

---

## Getting Started

### Prerequisites

Install the KAS build tool:
```bash
pip3 install kas
```

If you plan to run `npm run flash`, install host partition tools:
```bash
sudo apt-get install cloud-utils-growpart e2fsprogs
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

### Cache Directories

By default, Yocto downloads/sstate/ccache live under `yocto/`. To keep caches
outside the repo (useful for CI or clean checkouts), set `DIRTSIM_CACHE_ROOT`
before running `kas` or the Docker build:

```bash
export DIRTSIM_CACHE_ROOT=/home/data/linux1/yocto-cache
```

### Building in Docker

If you want an isolated build environment, use the Docker builder:

```bash
cd yocto
npm run docker-build
```

### x86 Runtime Container (CI)

Nightly CI builds an x86_64 Yocto rootfs and packages it as a Docker runtime image
for fast PR integration checks. The KAS config is `kas-dirtsim-x86.yml`, and the
runtime Dockerfile lives in `yocto/runtime/`.

The runtime entrypoint starts Xvfb (X11) by default, ensures `/data/dirtsim`
exists, and seeds fonts from `/usr/share/dirtsim/fonts` when needed. Useful
environment overrides:

- `DIRTSIM_OS_BACKEND=local` (os-manager local process backend)
- `DIRTSIM_UI_ARGS` / `DIRTSIM_UI_BACKEND` (UI launch args / backend)
- `DIRTSIM_UI_DISPLAY` (X11 display, default `:99`)
- `DIRTSIM_WORKDIR` (default `/data/dirtsim`)
- `DIRTSIM_XVFB_SCREEN` (default `1280x720x24`)

Local build example:

```bash
cd yocto
npm run runtime-image
```

Run functional tests inside the x86 runtime container:

```bash
cd yocto
npm run runtime-functional-tests
```

Target a single test or ensure the image is pulled/built first:

```bash
cd yocto
npm run runtime-functional-tests -- --test canExit
npm run runtime-functional-tests -- --ensure --build-if-missing
```

Publish to a local registry (oldman-desktop.local):

```bash
cd yocto
npm run runtime-image -- --build --publish --ensure-registry \
  --registry oldman-desktop.local:5000 \
  --registry-data-dir /data/dirtsim-registry
```

Useful options:
- `npm run docker-build -- --clean` to force a rebuild of the image sstate.
- `npm run docker-build -- --shell` to open an interactive shell in the container.

The output image will be at:
```
build/tmp/deploy/images/raspberrypi-dirtsim/dirtsim-image-raspberrypi-dirtsim.rootfs.wic.gz
```

### Flashing

The flash tool writes the image, injects your SSH public key, and sets the device hostname.

```bash
npm run flash                       # Use config file or interactive
npm run flash -- --interactive      # Force interactive mode (ignore config)
npm run flash -- --device /dev/sdb  # Direct flash (still confirms)
npm run flash -- --list             # Just list available devices
npm run flash -- --dry-run          # Show what would happen
npm run flash -- --reconfigure      # Re-select SSH key
```

Host requirements for flashing:
- `sudo` access for mount/partition operations
- `cloud-utils-growpart` (`growpart` command)
- `e2fsprogs` (`e2fsck` and `resize2fs` commands)

If these are missing, the script exits before writing to disk.

**Configuration File:**

The flash script uses `.flash-config.json` to store preferences. To set up automated flashing:

```bash
cp .flash-config.json.example .flash-config.json
# Edit .flash-config.json with your preferences
```

Supported config fields:
- `ssh_key_path` (required): Path to SSH public key
- `device` (optional): Auto-select device (e.g., `/dev/sda`)
- `device_serial` (auto): Stored serial number for device identity verification
- `hostname` (optional): Pre-set hostname
- `backup_data` (optional): Auto-backup data partition (`true`/`false`)
- `skip_confirmation` (optional): Skip final confirmation (`true`/`false`)

Use `--interactive` to override the config file and force interactive prompts.

**Safety Features:**

The flash script includes safety checks to prevent accidentally flashing the wrong drive:

- **Device identity verification:** After your first flash, the script saves the device's serial number. If a different physical device later appears at the same path (e.g., you unplugged your SD card and plugged in an external drive), you must type "YES" to confirm.
- **Large device warning:** Devices larger than 200GB require typing "YES" to flash, since these are unlikely to be SD cards or small USB drives.

**First-time setup:** The script prompts you to:
1. Select an SSH public key from `~/.ssh/` (saved to `.flash-config.json`)
2. Enter a hostname for this device (e.g., `dirtsim1`, `dirtsim2`)
3. Enter WiFi credentials (or skip if reflashing with existing data partition)

The hostname is written to `/boot/hostname.txt` and applied on first boot. You can also manually edit this file on the SD card before booting.

**WiFi Credentials:**

For automated flashing, create a `wifi-creds.local` file:
```bash
cp pi-base/wifi-creds.local.example wifi-creds.local
# Edit wifi-creds.local with your SSID and password
```

The file format is JSON:
```json
{
  "ssid": "MyNetworkName",
  "password": "MySecretPassword"
}
```

If the file doesn't exist, the script prompts interactively. WiFi credentials are stored in the `/data` partition and persist across A/B updates.

### Remote Update (A/B System)

The image uses **A/B partitions** for safe remote updates - no more corrupted filesystems! The disk has two rootfs partitions (sda2/sda3). Updates are written to the inactive partition while the system runs from the active one.

Host convention in this repo:
- `garden.local`: Dedicated x86 long-term training box.
- `dirtsim.local`: Default device host.
- Any host/IP can be used with `--target`.

```bash
npm run yolo                            # Build + flash to inactive slot + reboot
npm run yolo -- --target 192.168.1.50   # Target a specific host (default: dirtsim.local)
npm run yolo -- --docker                # Build inside the Docker image
npm run yolo -- --fast                  # Fast dev deploy (~10s, see below)
npm run yolo -- --clean                 # Force rebuild (cleans sstate first)
npm run yolo -- --skip-build            # Flash existing image (skip kas build)
npm run yolo -- --hold-my-mead          # Skip confirmation prompt (for scripts)
npm run yolo -- --dry-run               # Show what would happen
```

Dedicated trainer setup (`garden.local`, one-time x86 provisioning):

```bash
cd yocto
npm run provision-x86 -- --target garden.local
npm run yolo -- --target garden.local --fast --hold-my-mead
```

Trainer status checks:

```bash
ssh garden.local "dirtsim-cli server StatusGet"
ssh garden.local "dirtsim-cli ui StatusGet"
ssh garden.local "sudo journalctl -u dirtsim-server.service --since '5 min ago' --no-pager | grep -E 'Evolution: gen=|Generation [0-9]+ complete' | tail -n 20"

# Optional: query trainer-only server commands from workstation CLI.
ssh -N -L 28080:localhost:8080 garden.local
cd apps
./build-debug/bin/cli --address ws://localhost:28080 server TrainingResultList
./build-debug/bin/cli --address ws://localhost:28080 server TrainingResultGet '{"trainingSessionId":"<uuid>"}'
```

### Fast Dev Deploy (--fast)

For rapid iteration on application code (~10s instead of ~2min):

```bash
./update.sh --target dirtsim.local --fast   # Build + deploy + restart (~10s)
```

**What it does:**
1. Runs `ninja` directly (bypasses kas/bitbake overhead)
2. Strips binaries (109MB → 5.5MB, 151MB → 3.8MB)
3. SCPs stripped binaries to Pi
4. Restarts services (no reboot needed)

**When to use:**
- App code changes only (C++ in `apps/`)
- Pi is already running with SSH access
- You ran a full build at least once (ninja build dirs exist)

**When NOT to use:**
- Kernel, systemd, or new package changes (need full update)
- First deployment to a new device (need flash)

### Quick Deploy (Legacy)

The older `npm run deploy` also works but is slower (~60s) as it still uses bitbake:

```bash
npm run deploy          # Deploy both server and UI (~60s)
npm run deploy server   # Deploy server only
npm run deploy ui       # Deploy UI only
```

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
    IdentityFile ~/.ssh/id_ed25519_dirtsim
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
```

Then just: `ssh dirtsim`

---

## Partition Layout

The image uses **A/B partitions** for safe remote updates, plus a **persistent data partition** for WiFi credentials and config:

```
/dev/sda1  - boot (150MB, FAT32, shared by both slots)
/dev/sda2  - rootfs_a (2GB, ext4)
/dev/sda3  - rootfs_b (2GB, ext4)
/dev/sda4  - data (100MB, ext4, persistent across updates)
```

At any time, one partition is **active** (currently running) and the other is **inactive** (ready for updates). When you run `npm run yolo`, the system:
1. Writes the new image to the inactive partition
2. Switches the boot flag to use the newly written partition
3. Reboots into the new slot

**The data partition survives all updates** - both A/B updates and full reflashes. WiFi credentials configured via `nmcli`/`nmtui` are stored in `/data/NetworkManager/system-connections/` and bind-mounted into place on boot.

**Benefits:**
- Updates never corrupt the running system
- Instant rollback by switching boot slots
- No downtime risk during updates
- WiFi credentials persist across updates (no reconfiguration needed!)

**Check status:**
```bash
ssh dirtsim "ab-boot-manager status"
```

Output shows current slot, inactive slot, and data partition mount status.

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
├── wifi-creds.local          # WiFi credentials (gitignored, create from example)
├── scripts/
│   ├── deploy-apps.mjs       # Quick deploy userspace apps
│   ├── flash.mjs             # Flash image + inject SSH key + WiFi
│   ├── update.mjs            # Build + flash + verify (sneakernet)
│   └── yolo-update.mjs       # Remote update over network
├── pi-base/                  # Shared infrastructure (git submodule)
│   ├── yocto/meta-pi-base/   # Shared Yocto layer
│   │   ├── wic/sdimage-ab.wks.in  # A/B partition layout
│   │   └── recipes-support/  # ab-boot, persistent-data, hostname-setup
│   ├── scripts/lib/          # Shared flash utilities (JS)
│   └── wifi-creds.local.example
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
│   │   └── dirtsim/          # Server and UI recipes
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
| meta-pi-base | Shared infrastructure (A/B boot, persistent data, hostname) |
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

## Architecture & Design Decisions

### Unified Pi4/Pi5 Image

A single image works on both Pi4 and Pi5 by using hardware auto-detection:

**Boot-time detection:**
- `/proc/device-tree/model` contains Pi model string
- `dirtsim-detect-display.service` reads model and writes `/etc/dirtsim/display.conf`
- UI service loads config via `EnvironmentFile=-/etc/dirtsim/display.conf`

**Per-hardware configuration:**
- **Pi4:** 90° rotation, `/dev/input/touchscreen0` (ADS7846)
- **Pi5:** 270° rotation, `/dev/input/by-path/platform-i2c@0-event` (Goodix)

**config.txt conditional sections:**
```
[pi4]
dtoverlay=vc4-kms-v3d-pi4
hdmi_cvt 480 800 60 6 0 0 0
dtoverlay=ads7846,...

[pi5]
dtoverlay=vc4-kms-v3d-pi5
dtoverlay=vc4-kms-dpi-hyperpixel4
```

The bootloader auto-selects the appropriate section based on detected hardware.

### LVGL Direct Framebuffer

UI uses LVGL's fbdev backend - no Wayland compositor needed!

**Why this works:**
- LVGL renders directly to `/dev/fb0`
- KMS drivers (`vc4-kms-v3d`) provide framebuffer device
- Much simpler than Wayland, eliminates whole middleware layer

**Gotcha:** Framebuffer getty overwrites LVGL - must mask `getty@tty1.service`.

### A/B Partition System

Safe remote updates via dual rootfs partitions plus persistent data:

**Layout:**
```
/dev/sda1 - boot (150MB, shared)
/dev/sda2 - rootfs_a (2GB)
/dev/sda3 - rootfs_b (2GB)
/dev/sda4 - data (100MB, persistent)
```

**Update flow:**
1. Write new image to inactive partition (system keeps running)
2. Switch boot flag
3. Reboot into updated partition
4. Data partition remains untouched - WiFi credentials survive

**Full reflash flow:**
When running `npm run flash`, the script:
1. Detects existing data partition on the disk
2. Offers to backup `/data` before flashing
3. Flashes the new image (overwrites everything)
4. Restores `/data` contents to the new partition 4

**Tools (from pi-base layer):**
- `ab-boot-manager` - manages active/inactive slots, shows data partition status
- `ab-update` - wrapper for safe partition flashing
- `persistent-data` - systemd units for mounting `/data` and bind-mounting NetworkManager connections
- `hostname-setup` - sets hostname from `/boot/hostname.txt` at boot

**BusyBox compatibility:** Uses `/proc/cmdline` parsing instead of `findmnt`.

### Security Model

SSH key authentication only - no passwords:

- User: `dirtsim` (passwordless sudo)
- Root: Disabled for SSH
- SSH keys injected at flash time
- **Gotcha:** Must unlock account with `usermod -p '*'` or SSH rejects keys

### Hostname Customization

Each device gets unique hostname via `/boot/hostname.txt`:

1. Flash script prompts for hostname (default: `dirtsim`)
2. Writes hostname to `/boot/hostname.txt`
3. `hostname-setup` service (from pi-base) reads file on boot
4. Validates and applies hostname

Alternative: Manually edit `/boot/hostname.txt` before first boot.

### Key Gotchas

**GCC 13 strictness:** Yocto's GCC 13 is stricter than desktop - value-initialize template variables to silence `-Wmaybe-uninitialized`.

**Volatile log fix:** Yocto defaults to tmpfs `/var/log`. Fix: `VOLATILE_LOG_DIR = "no"` + journald `Storage=persistent`.

**USB boot fstab:** Default expects SD card (`/dev/mmcblk0p1`). For USB boot, use custom fstab with `/dev/sda1`.

**BusyBox limitations:**
- `dd` doesn't support GNU long options
- `df` doesn't support `-B1` (use `-k`)
- `reboot -f` doesn't exist (symlink to systemctl)
