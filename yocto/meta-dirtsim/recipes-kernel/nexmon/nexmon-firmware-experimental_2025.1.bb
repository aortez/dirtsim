SUMMARY = "Experimental Nexmon firmware blob for bcm43455"
DESCRIPTION = "Installs the Kali-packaged Nexmon 43455 standard firmware blob for scanner-mode experiments."
HOMEPAGE = "https://gitlab.com/kalilinux/packages/firmware-nexmon"

# This recipe fetches third-party firmware from Kali's firmware-nexmon packaging repo.
# Confirm redistribution terms before shipping images.
LICENSE = "binary-redist-Cypress"
LIC_FILES_CHKSUM = "file://debian/copyright;md5=cab0dab2da82a183cc2f3fa84e75c7b4"
NO_GENERIC_LICENSE[binary-redist-Cypress] = "debian/copyright"
LICENSE_FLAGS = "nexmon-firmware"
COMPATIBLE_MACHINE = "^raspberrypi-dirtsim$"

SRC_URI = "git://gitlab.com/kalilinux/packages/firmware-nexmon.git;protocol=https;branch=kali/master"
SRCREV = "c2eac5551dd0020e3a4453197c0cfad56cb181f2"

S = "${WORKDIR}/git"

do_install() {
    install -d ${D}${libdir}/nexmon
    install -m 0644 ${S}/cypress/cyfmac43455-sdio-standard.bin ${D}${libdir}/nexmon/cyfmac43455-sdio-standard.bin
}

FILES:${PN} = "${libdir}/nexmon/cyfmac43455-sdio-standard.bin"
