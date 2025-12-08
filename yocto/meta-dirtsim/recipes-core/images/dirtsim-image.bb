SUMMARY = "Dirt Simulation base image for Raspberry Pi"
DESCRIPTION = "A minimal console image with NetworkManager, SSH, and \
development tools for the Sparkle Duck dirt simulation project."
LICENSE = "MIT"

inherit core-image

# ============================================================================
# Image Features
# ============================================================================
# ssh-server-openssh: OpenSSH server for remote access.
# NOTE: debug-tweaks removed for security - we use SSH keys instead.
IMAGE_FEATURES += " \
    ssh-server-openssh \
"

# ============================================================================
# User Configuration
# ============================================================================
# Create 'dirtsim' user with sudo access.  SSH key is injected at flash time.
inherit extrausers

EXTRA_USERS_PARAMS = " \
    useradd -m -s /bin/bash -G sudo dirtsim; \
    usermod -p '*' dirtsim; \
"

# Ensure sudo is installed.
IMAGE_INSTALL:append = " sudo"

# Set up dirtsim home directory with correct ownership and permissions.
setup_dirtsim_home() {
    # .ssh directory (key injected at flash time).
    install -d -m 700 ${IMAGE_ROOTFS}/home/dirtsim/.ssh
    touch ${IMAGE_ROOTFS}/home/dirtsim/.ssh/authorized_keys
    chmod 600 ${IMAGE_ROOTFS}/home/dirtsim/.ssh/authorized_keys

    # Fix ownership of entire home directory (including .profile from base-files).
    chown -R 1000:1000 ${IMAGE_ROOTFS}/home/dirtsim
}
ROOTFS_POSTPROCESS_COMMAND:append = " setup_dirtsim_home;"

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
