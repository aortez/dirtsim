SUMMARY = "Dirt Simulation base image for Raspberry Pi"
DESCRIPTION = "A minimal console image with NetworkManager, SSH, and \
development tools for the DirtSim physics simulation."
LICENSE = "MIT"

# Inherit pi-base-image for A/B boot, NetworkManager, SSH, Bluetooth, and WiFi provisioning.
inherit pi-base-image

# USB boot (not SD card).
BOOT_DEVICE = "sda"

# Set default hostname (can be overridden by /boot/hostname.txt at boot).
HOSTNAME_DEFAULT = "dirtsim"

# ============================================================================
# User Configuration
# ============================================================================
# Create 'dirtsim' user with sudo and input access. SSH key is injected at flash time.
inherit extrausers

EXTRA_USERS_PARAMS = " \
    useradd -m -s /bin/bash -G sudo,input dirtsim; \
    usermod -p '*' dirtsim; \
"

# Set up dirtsim home directory with correct ownership and permissions.
setup_dirtsim_home() {
    # .ssh directory (key injected at flash time).
    install -d -m 700 ${IMAGE_ROOTFS}/home/dirtsim/.ssh
    touch ${IMAGE_ROOTFS}/home/dirtsim/.ssh/authorized_keys
    chmod 600 ${IMAGE_ROOTFS}/home/dirtsim/.ssh/authorized_keys

    # DirtSim application directory (logs, config, etc.).
    install -d -m 755 ${IMAGE_ROOTFS}/home/dirtsim/dirtsim
    install -d -m 755 ${IMAGE_ROOTFS}/home/dirtsim/dirtsim/logs
    install -d -m 755 ${IMAGE_ROOTFS}/home/dirtsim/dirtsim/config

    # Fix ownership of entire home directory (including .profile from base-files).
    chown -R 1000:1000 ${IMAGE_ROOTFS}/home/dirtsim
}
ROOTFS_POSTPROCESS_COMMAND:append = " setup_dirtsim_home;"

# ============================================================================
# HyperPixel Backlight Fix
# ============================================================================
# The HyperPixel backlight doesn't auto-enable at boot. This service fixes it.
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
# Coredump Collection
# ============================================================================
# Enable crash dump collection for field debugging. Cores are compressed and
# stored in /var/lib/systemd/coredump/, accessible via coredumpctl.
# Note: coredumpctl is part of systemd when PACKAGECONFIG includes "coredump"
# (configured in kas-dirtsim.yml). No separate package needed.
setup_coredump_config() {
    install -d ${IMAGE_ROOTFS}/etc/systemd
    cat > ${IMAGE_ROOTFS}/etc/systemd/coredump.conf << 'EOF'
[Coredump]
Storage=external
Compress=yes
MaxUse=500M
KeepFree=100M
EOF

    # Store coredumps on /data partition (more space than rootfs).
    # The symlink points to /data/coredumps, created at boot via tmpfiles.d
    # (can't create in rootfs since /data is a separate mounted partition).
    rm -rf ${IMAGE_ROOTFS}/var/lib/systemd/coredump
    ln -s /data/coredumps ${IMAGE_ROOTFS}/var/lib/systemd/coredump

    # Create tmpfiles.d entry to make /data/coredumps at boot.
    install -d ${IMAGE_ROOTFS}/usr/lib/tmpfiles.d
    echo "d /data/coredumps 0755 root root -" > ${IMAGE_ROOTFS}/usr/lib/tmpfiles.d/dirtsim-coredump.conf
}
ROOTFS_POSTPROCESS_COMMAND:append = " setup_coredump_config;"

# ============================================================================
# Additional Development & Debug Tools
# ============================================================================
# Tools beyond what pi-base-image provides.
IMAGE_INSTALL:append = " \
    file \
    jq \
    nmon \
    rsync \
    screen \
    strace \
    tree \
    util-linux-agetty \
    vim \
"

# ============================================================================
# WiFi Firmware
# ============================================================================
# Firmware for the Pi's onboard WiFi.
IMAGE_INSTALL:append = " \
    linux-firmware-rpidistro-bcm43455 \
    linux-firmware-rpidistro-bcm43456 \
"

# ============================================================================
# DirtSim Application
# ============================================================================
# Headless physics simulation with WebSocket API and LVGL display client.
IMAGE_INSTALL:append = " \
    dirtsim-server \
    dirtsim-ui \
"
