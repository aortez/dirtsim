# Sparkle Duck Server - Headless dirt simulation physics server.
# Provides WebSocket API on port 8080 for remote control and world state queries.

SUMMARY = "Sparkle Duck dirt simulation server"
DESCRIPTION = "Headless physics simulation server with WebSocket API for the Sparkle Duck project."
HOMEPAGE = "https://github.com/sparkle-duck/sparkle-duck"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Build from local source (externalsrc).
inherit externalsrc cmake systemd

EXTERNALSRC = "${THISDIR}/../../../../dirtsim"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

# Dependencies.
DEPENDS = " \
    avahi \
    boost \
    libyuv \
    openh264 \
    openssl \
    pkgconfig-native \
"

# CMake configuration for server-only build.
EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_FULLY_DISCONNECTED=OFF \
"

# Allow network access during configure for FetchContent.
do_configure[network] = "1"

# We only want the server binary, not the UI or tests.
do_compile() {
    cmake --build ${B} --target sparkle-duck-server -- ${PARALLEL_MAKE}
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/bin/sparkle-duck-server ${D}${bindir}/

    # Install systemd service (system-level, runs as dirtsim user).
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${THISDIR}/files/sparkle-duck-server.service ${D}${systemd_system_unitdir}/
}

# Enable the systemd user service.
SYSTEMD_SERVICE:${PN} = "sparkle-duck-server.service"
SYSTEMD_AUTO_ENABLE = "enable"

# Package the binary.
FILES:${PN} = " \
    ${bindir}/sparkle-duck-server \
    ${systemd_system_unitdir}/sparkle-duck-server.service \
"

# Runtime dependencies.
RDEPENDS:${PN} = " \
    avahi-daemon \
    libyuv \
    openh264 \
"
