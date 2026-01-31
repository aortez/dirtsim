# DirtSim Audio - SDL2-based synth process for beeps and cues.

SUMMARY = "DirtSim audio synth service"
DESCRIPTION = "SDL2 audio synth process with WebSocket control for DirtSim."
HOMEPAGE = "https://github.com/aortez/dirtsim"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit externalsrc cmake systemd

EXTERNALSRC = "${THISDIR}/../../../../apps"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

SRC_URI = " \
    file://dirtsim-audio.service \
"

DEPENDS = " \
    boost \
    freetype \
    libsdl2 \
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
"

INHIBIT_PACKAGE_STRIP = "1"

# Allow network access during configure for FetchContent.
do_configure[network] = "1"

do_compile() {
    cmake --build ${B} --target dirtsim-audio -- ${PARALLEL_MAKE}
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/bin/dirtsim-audio ${D}${bindir}/

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/dirtsim-audio.service ${D}${systemd_system_unitdir}/
}

SYSTEMD_SERVICE:${PN} = "dirtsim-audio.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN} = " \
    ${bindir}/dirtsim-audio \
    ${systemd_system_unitdir}/dirtsim-audio.service \
"

RDEPENDS:${PN} = " \
    alsa-lib \
    alsa-plugins \
    libnm \
    libsdl2 \
    libyuv \
    openh264 \
    sqlite3 \
"
