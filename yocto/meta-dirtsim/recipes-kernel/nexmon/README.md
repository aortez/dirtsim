# Nexmon (Third-Party Code)

This directory contains third-party sources and recipes used to build an experimental Wi-Fi scanner stack for DirtSim devices.

**Important**
- Files under `yocto/meta-dirtsim/recipes-kernel/nexmon/` are **not** covered by DirtSim's "Sparkle Duck License" (`/LICENSE`).
- Each component below is distributed under its **own upstream license terms**. Preserve file headers and comply with the applicable licenses when distributing images.

## Components

### `nexmon-brcmfmac-experimental` (kernel module)
- Purpose: Builds an alternate `brcmfmac.ko` used for monitor-mode experiments.
- Upstream: Kali packaging of Nexmon `brcmfmac-nexmon-dkms` (see recipe `nexmon-brcmfmac-experimental_6.12.2.bb`).
- License: See `nexmon-brcmfmac-experimental/brcmfmac-nexmon-dkms/debian/copyright`.

### `nexutil-experimental` (userspace tool)
- Purpose: Builds `nexutil` used to toggle monitor/promiscuous mode during scanner mode.
- Upstream: NexMon utilities tree (see recipe `nexutil-experimental_2025.1.bb`).
- License: GPL notice headers are present in the vendored sources (e.g. `nexutil/nexutil.c`).

### `nexmon-firmware-experimental` (firmware blob)
- Purpose: Installs an experimental firmware blob used with the alternate stack.
- Upstream: Kali packaging of Nexmon firmware (see recipe `nexmon-firmware-experimental_2025.1.bb`).
- License: Treated as a binary blob. Confirm redistribution terms before shipping images.

### `dirtsim-nexmon-mode` (helper)
- Purpose: Switches between stock and experimental stacks on device.
- Note: This is a reversible safety mechanism; use Ethernet as a recovery path whenever possible.

