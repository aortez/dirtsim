# Add passwordless sudo for dirtsim user.

FILESEXTRAPATHS:prepend := "${THISDIR}/sudo:"

SRC_URI:append = " file://dirtsim-sudoers"

do_install:append() {
    install -d ${D}${sysconfdir}/sudoers.d
    install -m 0440 ${WORKDIR}/dirtsim-sudoers ${D}${sysconfdir}/sudoers.d/dirtsim
}
