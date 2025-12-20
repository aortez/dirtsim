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

    # Sparkle Duck application directory (logs, config, etc.).
    install -d -m 755 ${IMAGE_ROOTFS}/home/dirtsim/sparkle-duck
    install -d -m 755 ${IMAGE_ROOTFS}/home/dirtsim/sparkle-duck/logs
    install -d -m 755 ${IMAGE_ROOTFS}/home/dirtsim/sparkle-duck/config

    # Fix ownership of entire home directory (including .profile from base-files).
    chown -R 1000:1000 ${IMAGE_ROOTFS}/home/dirtsim
}
ROOTFS_POSTPROCESS_COMMAND:append = " setup_dirtsim_home;"

# ============================================================================
# A/B Boot Initialization
# ============================================================================
# On first boot, mark that we're running from slot A.
setup_ab_boot() {
    # Create initial boot_slot marker (will be on boot partition after flash).
    # This gets copied to /boot when the boot partition is mounted.
    install -d ${IMAGE_ROOTFS}/boot
    echo "a" > ${IMAGE_ROOTFS}/boot/boot_slot
}
ROOTFS_POSTPROCESS_COMMAND:append = " setup_ab_boot;"

# ============================================================================
# HyperPixel Backlight Fix
# ============================================================================
# The HyperPixel backlight doesn't auto-enable at boot.  This service fixes it.
setup_hyperpixel_backlight() {
    install -d ${IMAGE_ROOTFS}/etc/systemd/system

    # Create the service file.
    cat > ${IMAGE_ROOTFS}/etc/systemd/system/hyperpixel-backlight.service << 'EOF'
[Unit]
Description=Enable HyperPixel backlight
After=systemd-modules-load.service
DefaultDependencies=no

[Service]
Type=oneshot
ExecStart=/bin/sh -c 'echo 0 > /sys/class/backlight/backlight/bl_power; echo 1 > /sys/class/backlight/backlight/brightness'
RemainAfterExit=yes

[Install]
WantedBy=sysinit.target
EOF

    # Enable the service.
    install -d ${IMAGE_ROOTFS}/etc/systemd/system/sysinit.target.wants
    ln -sf ../hyperpixel-backlight.service ${IMAGE_ROOTFS}/etc/systemd/system/sysinit.target.wants/hyperpixel-backlight.service
}
ROOTFS_POSTPROCESS_COMMAND:append = " setup_hyperpixel_backlight;"

# ============================================================================
# Disable Framebuffer Console
# ============================================================================
# The UI uses the framebuffer directly via LVGL. Disable the login console
# on tty1 so it doesn't overwrite the UI with text.
disable_fb_console() {
    # Mask getty@tty1 so it doesn't start.
    install -d ${IMAGE_ROOTFS}/etc/systemd/system
    ln -sf /dev/null ${IMAGE_ROOTFS}/etc/systemd/system/getty@tty1.service
}
ROOTFS_POSTPROCESS_COMMAND:append = " disable_fb_console;"

# ============================================================================
# Network Management
# ============================================================================
# NetworkManager provides nmcli/nmtui for network configuration.
IMAGE_INSTALL:append = " \
    networkmanager \
    networkmanager-nmtui \
    networkmanager-nmcli \
"

# ============================================================================
# Persistent Data
# ============================================================================
# Mounts /data partition which survives A/B updates.
# WiFi credentials, logs, and config are stored here.
IMAGE_INSTALL:append = " \
    persistent-data \
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
    ab-boot-manager \
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
# Stage 2: Dirt Simulation Server
# ============================================================================
# Headless physics simulation with WebSocket API on port 8080.
IMAGE_INSTALL:append = " \
    sparkle-duck-server \
"

# ============================================================================
# Stage 3: Dirt Simulation UI
# ============================================================================
# LVGL-based display client using framebuffer backend (no compositor needed).
# Connects to server on localhost:8080 and renders the simulation.
IMAGE_INSTALL:append = " \
    sparkle-duck-ui \
"
