SUMMARY = "Experimental Nexmon brcmfmac module for Raspberry Pi 5"
DESCRIPTION = "Builds the Kali-packaged Nexmon brcmfmac driver as an experimental alternate Wi-Fi stack."
HOMEPAGE = "https://gitlab.com/kalilinux/packages/brcmfmac-nexmon-dkms"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://debian/copyright;md5=2715adf71522b4b3beae400f6305e8c7"
COMPATIBLE_MACHINE = "^raspberrypi-dirtsim$"

inherit module

# The kernel rejects this module if Yocto strips it during packaging.
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_SYSROOT_STRIP = "1"

SRC_URI = "file://brcmfmac-nexmon-dkms/"

S = "${WORKDIR}/brcmfmac-nexmon-dkms"
B = "${S}"

EXTRA_OEMAKE += " \
    KERNEL_SRC=${STAGING_KERNEL_BUILDDIR} \
    KSRC=${STAGING_KERNEL_BUILDDIR} \
    KVER=${KERNEL_VERSION} \
"

do_install() {
    install -d ${D}${libdir}/nexmon
    install -m 0644 ${B}/brcmfmac.ko ${D}${libdir}/nexmon/brcmfmac.ko
}

FILES:${PN} = "${libdir}/nexmon/brcmfmac.ko"
ALLOW_EMPTY:${PN} = "0"
