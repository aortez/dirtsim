SUMMARY = "Dirt Simulation base image for Raspberry Pi"
DESCRIPTION = "A minimal console image with NetworkManager, SSH, and \
development tools for the Sparkle Duck dirt simulation project."
LICENSE = "MIT"

inherit core-image

# ============================================================================
# Image Features
# ============================================================================
# ssh-server-openssh: OpenSSH server for remote access.
# debug-tweaks: Allow root login, empty password (development only!).
IMAGE_FEATURES += " \
    ssh-server-openssh \
    debug-tweaks \
"

# ============================================================================
# Network Management
# ============================================================================
# NetworkManager provides nmcli/nmtui for familiar network configuration.
# networkmanager-nmtui: The curses-based UI we know and love.
IMAGE_INSTALL:append = " \
    networkmanager \
    networkmanager-nmtui \
    networkmanager-nmcli \
"

# ============================================================================
# Service Discovery
# ============================================================================
# Avahi for mDNS - find the Pi as "dirtsim.local" on the network.
IMAGE_INSTALL:append = " \
    avahi-daemon \
    avahi-utils \
"

# ============================================================================
# Development & Debug Tools
# ============================================================================
# Useful tools for poking around on the device.
IMAGE_INSTALL:append = " \
    curl \
    file \
    htop \
    jq \
    less \
    nano \
    nmon \
    rsync \
    screen \
    strace \
    tree \
    util-linux-agetty \
    vim \
"

# ============================================================================
# WiFi Support
# ============================================================================
# Firmware for the Pi's onboard WiFi.
IMAGE_INSTALL:append = " \
    linux-firmware-rpidistro-bcm43455 \
    linux-firmware-rpidistro-bcm43456 \
"

# ============================================================================
# Future Stages (commented for now)
# ============================================================================
# Stage 2: Graphics
# IMAGE_INSTALL:append = " wayland weston"

# Stage 3: Dirt Simulation
# IMAGE_INSTALL:append = " sparkle-duck"
