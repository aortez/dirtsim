SUMMARY = "Experimental Nexmon firmware blob for bcm43455"
DESCRIPTION = "Installs the Kali-packaged Nexmon 43455 standard firmware blob for scanner-mode experiments."
HOMEPAGE = "https://gitlab.com/kalilinux/packages/firmware-nexmon"
LICENSE = "CLOSED"
COMPATIBLE_MACHINE = "^raspberrypi-dirtsim$"

SRC_URI = "file://cyfmac43455-sdio-standard.bin"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${libdir}/nexmon
    install -m 0644 ${WORKDIR}/cyfmac43455-sdio-standard.bin ${D}${libdir}/nexmon/cyfmac43455-sdio-standard.bin
}

FILES:${PN} = "${libdir}/nexmon/cyfmac43455-sdio-standard.bin"
