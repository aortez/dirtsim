# Make /var/log persistent (not a tmpfs symlink).
# This allows journald to store logs across reboots.
VOLATILE_LOG_DIR = "no"

# Custom fstab for USB boot (uses /dev/sda1 for /boot instead of mmcblk0p1).
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
SRC_URI += "file://fstab"

do_install:append() {
    install -m 0644 ${WORKDIR}/fstab ${D}${sysconfdir}/fstab
}
