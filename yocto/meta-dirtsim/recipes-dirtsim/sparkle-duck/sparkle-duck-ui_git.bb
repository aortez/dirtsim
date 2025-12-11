# Sparkle Duck UI - LVGL-based display client for the dirt simulation.
# Connects to the server on port 8080 and renders the world state.
# Uses framebuffer backend for direct display output (no compositor needed).

SUMMARY = "Sparkle Duck simulation UI"
DESCRIPTION = "LVGL-based display client that connects to the Sparkle Duck server and renders the simulation."
HOMEPAGE = "https://github.com/sparkle-duck/sparkle-duck"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Build from local source (externalsrc).
inherit externalsrc cmake systemd

EXTERNALSRC = "${THISDIR}/../../../../dirtsim"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

# Service file (outside EXTERNALSRC, so must be in SRC_URI for proper tracking).
SRC_URI = "file://sparkle-duck-ui.service"

# Dependencies.
DEPENDS = " \
    avahi \
    boost \
    libdrm \
    libinput \
    libyuv \
    openh264 \
    openssl \
    pkgconfig-native \
    xkeyboard-config \
"

# CMake configuration for UI build.
EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_FULLY_DISCONNECTED=OFF \
"

# Allow network access during configure for FetchContent.
do_configure[network] = "1"

# Build the UI binary.
do_compile() {
    cmake --build ${B} --target sparkle-duck-ui -- ${PARALLEL_MAKE}
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/bin/sparkle-duck-ui ${D}${bindir}/

    # Install systemd service (from WORKDIR, fetched via SRC_URI).
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/sparkle-duck-ui.service ${D}${systemd_system_unitdir}/
}

# Enable the systemd service.
SYSTEMD_SERVICE:${PN} = "sparkle-duck-ui.service"
SYSTEMD_AUTO_ENABLE = "enable"

# Package the binary.
FILES:${PN} = " \
    ${bindir}/sparkle-duck-ui \
    ${systemd_system_unitdir}/sparkle-duck-ui.service \
"

# Runtime dependencies.
RDEPENDS:${PN} = " \
    avahi-daemon \
    libdrm \
    libinput \
    libyuv \
    openh264 \
    sparkle-duck-server \
    xkeyboard-config \
"
