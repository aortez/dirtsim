# DirtSim UI - LVGL-based display client for the dirt simulation.
# Connects to the server on port 8080 and renders the world state.
# Uses framebuffer backend for direct display output (no compositor needed).

SUMMARY = "DirtSim simulation UI"
DESCRIPTION = "LVGL-based display client that connects to the DirtSim server and renders the simulation."
HOMEPAGE = "https://github.com/aortez/dirtsim"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Build from local source (externalsrc).
inherit externalsrc cmake systemd

EXTERNALSRC = "${THISDIR}/../../../../apps"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

# Service files (outside EXTERNALSRC, so must be in SRC_URI for proper tracking).
SRC_URI = " \
    file://dirtsim-ui.service \
    file://dirtsim-detect-display.service \
    file://dirtsim-detect-display.sh \
    file://dirtsim-set-hostname.service \
    file://dirtsim-set-hostname.sh \
    file://dirtsim-config-setup.service \
    file://dirtsim-config-setup.sh \
"

# Dependencies.
DEPENDS = " \
    avahi \
    boost \
    freetype \
    libdrm \
    libinput \
    libsdl2 \
    libyuv \
    openh264 \
    openssl \
    pkgconfig-native \
    xkeyboard-config \
"

# CMake configuration for UI build.
# RelWithDebInfo keeps -O2 optimization but adds -g debug symbols for crash analysis.
EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DFETCHCONTENT_FULLY_DISCONNECTED=OFF \
"

# Keep debug symbols in deployed binary (don't strip).
INHIBIT_PACKAGE_STRIP = "1"

# Allow network access during configure for FetchContent.
do_configure[network] = "1"

# Build the UI and CLI binaries.
do_compile() {
    cmake --build ${B} --target dirtsim-ui cli -- ${PARALLEL_MAKE}
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/bin/dirtsim-ui ${D}${bindir}/
    install -m 0755 ${B}/bin/cli ${D}${bindir}/dirtsim-cli

    # Install system configuration scripts.
    install -m 0755 ${WORKDIR}/dirtsim-detect-display.sh ${D}${bindir}/
    install -m 0755 ${WORKDIR}/dirtsim-set-hostname.sh ${D}${bindir}/
    install -m 0755 ${WORKDIR}/dirtsim-config-setup.sh ${D}${bindir}/

    # Install systemd services (from WORKDIR, fetched via SRC_URI).
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/dirtsim-ui.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/dirtsim-detect-display.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/dirtsim-set-hostname.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/dirtsim-config-setup.service ${D}${systemd_system_unitdir}/

    # Install default UI configuration.
    install -d ${D}${sysconfdir}/dirtsim
    install -m 0644 ${EXTERNALSRC}/config/ui.json ${D}${sysconfdir}/dirtsim/ui.json

    # Install fonts (for color emoji clock display).
    install -d ${D}/data/dirtsim/fonts
    install -m 0644 ${EXTERNALSRC}/fonts/NotoColorEmoji.ttf ${D}/data/dirtsim/fonts/

    # Install FontAwesome for icon rendering.
    install -d ${D}/usr/share/fonts/fontawesome
    install -m 0644 ${EXTERNALSRC}/assets/fonts/fa-solid-900.ttf ${D}/usr/share/fonts/fontawesome/
}

# Enable the systemd services.
SYSTEMD_SERVICE:${PN} = "dirtsim-ui.service dirtsim-detect-display.service dirtsim-set-hostname.service dirtsim-config-setup.service"
SYSTEMD_AUTO_ENABLE = "enable"

# Package the binaries.
FILES:${PN} = " \
    ${bindir}/dirtsim-cli \
    ${bindir}/dirtsim-ui \
    ${bindir}/dirtsim-detect-display.sh \
    ${bindir}/dirtsim-set-hostname.sh \
    ${bindir}/dirtsim-config-setup.sh \
    /data/dirtsim/fonts/NotoColorEmoji.ttf \
    /usr/share/fonts/fontawesome/fa-solid-900.ttf \
    ${sysconfdir}/dirtsim/ui.json \
    ${systemd_system_unitdir}/dirtsim-ui.service \
    ${systemd_system_unitdir}/dirtsim-detect-display.service \
    ${systemd_system_unitdir}/dirtsim-set-hostname.service \
    ${systemd_system_unitdir}/dirtsim-config-setup.service \
"

# Runtime dependencies.
RDEPENDS:${PN} = " \
    avahi-daemon \
    dirtsim-server \
    freetype \
    libdrm \
    libinput \
    libsdl2 \
    libyuv \
    openh264 \
    xkeyboard-config \
"
