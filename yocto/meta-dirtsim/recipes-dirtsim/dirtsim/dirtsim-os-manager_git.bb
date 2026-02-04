# DirtSim OS Manager - privileged process for system control and health reporting.

SUMMARY = "DirtSim OS Manager"
DESCRIPTION = "Privileged WebSocket service for system control and health reporting for DirtSim."
HOMEPAGE = "https://github.com/aortez/dirtsim"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit externalsrc cmake systemd

EXTERNALSRC = "${THISDIR}/../../../../apps"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

SRC_URI = " \
    file://dirtsim-os-manager.service \
"

DEPENDS = " \
    boost \
    freetype \
    libsdl2 \
    libssh2 \
    libyuv \
    networkmanager \
    openh264 \
    openssl \
    pkgconfig-native \
    sqlite3 \
"

EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DFETCHCONTENT_FULLY_DISCONNECTED=OFF \
    -DDIRTSIM_USE_SYSTEM_LIBSSH2=ON \
"

INHIBIT_PACKAGE_STRIP = "1"

# Allow network access during configure for FetchContent.
do_configure[network] = "1"

do_compile() {
    cmake --build ${B} --target dirtsim-os-manager -- ${PARALLEL_MAKE}
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/bin/dirtsim-os-manager ${D}${bindir}/

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/dirtsim-os-manager.service ${D}${systemd_system_unitdir}/
}

SYSTEMD_SERVICE:${PN} = "dirtsim-os-manager.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN} = " \
    ${bindir}/dirtsim-os-manager \
    ${systemd_system_unitdir}/dirtsim-os-manager.service \
"

RDEPENDS:${PN} = " \
    libnm \
    libyuv \
    openh264 \
    sqlite3 \
"
