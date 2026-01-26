SUMMARY = "Picade Max USB Audio firmware"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://src/main.cpp;beginline=1;endline=23;md5=7a07b78504a13b96d4b350b16cde4fa9"

SRC_URI = "\
    file://picade-max-audio-61eef4b.tar.gz \
    git://github.com/raspberrypi/pico-sdk.git;name=pico-sdk;protocol=https;branch=master;destsuffix=pico-sdk \
    git://github.com/raspberrypi/pico-extras.git;name=pico-extras;protocol=https;branch=master;destsuffix=pico-extras \
    git://github.com/pimoroni/pimoroni-pico.git;name=pimoroni-pico;protocol=https;branch=main;destsuffix=pimoroni-pico \
"

SRCREV_pico-sdk = "a1438dff1d38bd9c65dbd693f0e5db4b9ae91779"
SRCREV_pico-extras = "82409a94de00802105c84e5c06f333114bb8b316"
SRCREV_pimoroni-pico = "1e7fb9e723c18fea24aa9353e767cadee2a87d70"
SRCREV_FORMAT = "default_pico-sdk_pico-extras_pimoroni-pico"

S = "${WORKDIR}/picade-max-audio"

inherit cmake

DEPENDS += "cmake-native ninja-native"

EXTRA_OECMAKE += "\
    -DPICO_SDK_PATH=${WORKDIR}/pico-sdk \
    -DPICO_EXTRAS_PATH=${WORKDIR}/pico-extras \
    -DPIMORONI_PICO_PATH=${WORKDIR}/pimoroni-pico \
"

do_configure:prepend() {
    if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
        bbfatal "arm-none-eabi-gcc not found in PATH. Install an ARM GNU toolchain or provide it via PATH."
    fi
}

do_install() {
    install -d ${D}${datadir}/dirtsim/firmware
    install -m 0644 ${B}/picade-max-audio.uf2 ${D}${datadir}/dirtsim/firmware/picade-max-audio.uf2
}

FILES:${PN} += "${datadir}/dirtsim/firmware/picade-max-audio.uf2"
