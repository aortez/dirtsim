SUMMARY = "Experimental nexutil userspace tool for Raspberry Pi 5 scanner mode"
DESCRIPTION = "Builds nexutil from the Nexmon utilities tree so scanner mode can control the experimental Wi-Fi stack."
HOMEPAGE = "https://github.com/seemoo-lab/nexmon"
LICENSE = "CLOSED"
COMPATIBLE_MACHINE = "^raspberrypi-dirtsim$"

inherit pkgconfig

SRC_URI = " \
    file://libargp/ \
    file://libnexio/ \
    file://nexutil/ \
    file://patches/include/ \
"

S = "${WORKDIR}"
B = "${WORKDIR}/build"

DEPENDS = "libnl"

do_compile() {
    install -d ${B}
    pkg_config_cflags="$(pkg-config --cflags libnl-3.0 libnl-genl-3.0)"
    pkg_config_libs="$(pkg-config --libs libnl-3.0 libnl-genl-3.0)"
    version_macro='-DVERSION="experimental"'

    ${CC} ${CFLAGS} ${CPPFLAGS} \
        -DBUILD_ON_RPI \
        $version_macro \
        -I${S}/libargp \
        -I${S}/patches/include \
        -I${S}/nexutil/include \
        $pkg_config_cflags \
        -c ${S}/libnexio/libnexio.c \
        -o ${B}/libnexio.o

    ${AR} rcs ${B}/libnexio.a ${B}/libnexio.o

    ${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} \
        -DBUILD_ON_RPI \
        -DUSE_NETLINK \
        $version_macro \
        -I${S}/libargp \
        -I${S}/nexutil \
        -I${S}/nexutil/include \
        -I${S}/patches/include \
        ${S}/nexutil/nexutil.c \
        ${S}/nexutil/bcmutils.c \
        ${S}/nexutil/bcmwifi_channels.c \
        ${S}/nexutil/b64-encode.c \
        ${S}/nexutil/b64-decode.c \
        ${B}/libnexio.a \
        $pkg_config_cflags \
        $pkg_config_libs \
        -lpthread \
        -o ${B}/nexutil
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/nexutil ${D}${bindir}/nexutil
}

FILES:${PN} = "${bindir}/nexutil"
