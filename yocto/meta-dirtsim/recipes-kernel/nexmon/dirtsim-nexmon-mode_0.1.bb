SUMMARY = "Switch between stock and experimental Nexmon Wi-Fi stacks"
DESCRIPTION = "Installs a reversible on-device helper for enabling the experimental Nexmon Wi-Fi stack."
HOMEPAGE = "https://github.com/aortez/dirtsim"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
COMPATIBLE_MACHINE = "^raspberrypi-dirtsim$"

SRC_URI = "file://dirtsim-nexmon-mode"

S = "${WORKDIR}"

RDEPENDS:${PN} = " \
    iproute2 \
    iw \
    kmod \
    networkmanager \
    nexmon-brcmfmac-experimental \
    nexmon-firmware-experimental \
"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/dirtsim-nexmon-mode ${D}${bindir}/dirtsim-nexmon-mode
}

FILES:${PN} = "${bindir}/dirtsim-nexmon-mode"
