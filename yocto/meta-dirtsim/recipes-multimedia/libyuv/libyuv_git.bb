# libyuv - YUV conversion and scaling library from Google.
# Used for color space conversion in video encoding.

SUMMARY = "YUV conversion and scaling library"
DESCRIPTION = "libyuv is an open source project that includes YUV scaling and conversion functionality."
HOMEPAGE = "https://chromium.googlesource.com/libyuv/libyuv"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=464282cfb405b005b9637f11103a7325"

SRC_URI = "git://chromium.googlesource.com/libyuv/libyuv;protocol=https;branch=main"

# Use a recent stable commit.
SRCREV = "a8e59d207483f75b87dd5fc670e937672cdf5776"

S = "${WORKDIR}/git"

inherit cmake

# Build shared library.
EXTRA_OECMAKE = "-DBUILD_SHARED_LIBS=ON"

# Install the library and headers.
do_install() {
    install -d ${D}${libdir}
    install -d ${D}${includedir}/libyuv

    # Install shared library.
    install -m 0755 ${B}/libyuv.so* ${D}${libdir}/

    # Install headers.
    install -m 0644 ${S}/include/libyuv.h ${D}${includedir}/
    install -m 0644 ${S}/include/libyuv/*.h ${D}${includedir}/libyuv/
}

FILES:${PN} = "${libdir}/libyuv.so*"
FILES:${PN}-dev = "${includedir}"
