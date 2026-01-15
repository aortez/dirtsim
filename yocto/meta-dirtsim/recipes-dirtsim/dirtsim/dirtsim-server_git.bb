# DirtSim Server - Headless dirt simulation physics server.
# Provides WebSocket API on port 8080 for remote control and world state queries.

SUMMARY = "DirtSim physics simulation server"
DESCRIPTION = "Headless physics simulation server with WebSocket API for DirtSim."
HOMEPAGE = "https://github.com/aortez/dirtsim"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Build from local source (externalsrc).
inherit externalsrc cmake systemd

EXTERNALSRC = "${THISDIR}/../../../../apps"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

# Dependencies.
DEPENDS = " \
    avahi \
    boost \
    freetype \
    libsdl2 \
    libyuv \
    openh264 \
    openssl \
    pkgconfig-native \
    sqlite3 \
"

# CMake configuration for server-only build.
# RelWithDebInfo keeps -O2 optimization but adds -g debug symbols for crash analysis.
EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DFETCHCONTENT_FULLY_DISCONNECTED=OFF \
"

# Keep debug symbols in deployed binary (don't strip).
INHIBIT_PACKAGE_STRIP = "1"

# Allow network access during configure for FetchContent.
do_configure[network] = "1"

# We only want the server binary, not the UI or tests.
do_compile() {
    cmake --build ${B} --target dirtsim-server -- ${PARALLEL_MAKE}
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/bin/dirtsim-server ${D}${bindir}/

    # Install systemd service (system-level, runs as dirtsim user).
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${THISDIR}/files/dirtsim-server.service ${D}${systemd_system_unitdir}/

    # Install default configuration.
    install -d ${D}${sysconfdir}/dirtsim
    install -m 0644 ${EXTERNALSRC}/config/server.json ${D}${sysconfdir}/dirtsim/server.json
}

# Enable the systemd user service.
SYSTEMD_SERVICE:${PN} = "dirtsim-server.service"
SYSTEMD_AUTO_ENABLE = "enable"

# Package the binary.
FILES:${PN} = " \
    ${bindir}/dirtsim-server \
    ${systemd_system_unitdir}/dirtsim-server.service \
    ${sysconfdir}/dirtsim/server.json \
"

# Runtime dependencies.
RDEPENDS:${PN} = " \
    avahi-daemon \
    libsdl2 \
    libyuv \
    openh264 \
    sqlite3 \
"
