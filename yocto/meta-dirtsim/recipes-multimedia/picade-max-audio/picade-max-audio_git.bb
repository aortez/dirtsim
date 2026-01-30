SUMMARY = "Picade Max USB Audio firmware"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit allarch

DEPENDS += "picade-max-audio-build-native"

do_install() {
    install -d ${D}${datadir}/dirtsim/firmware
    install -m 0644 ${STAGING_DATADIR_NATIVE}/dirtsim/firmware/picade-max-audio.uf2 ${D}${datadir}/dirtsim/firmware/picade-max-audio.uf2
}

FILES:${PN} += "${datadir}/dirtsim/firmware/picade-max-audio.uf2"

do_configure[noexec] = "1"
do_compile[noexec] = "1"
