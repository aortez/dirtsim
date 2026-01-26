SUMMARY = "Picade Max USB Audio firmware builder (native)"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://src/main.cpp;beginline=1;endline=23;md5=7a07b78504a13b96d4b350b16cde4fa9"

SRC_URI = "\
    file://picade-max-audio-61eef4b.tar.gz \
    gitsm://github.com/raspberrypi/pico-sdk.git;name=pico-sdk;protocol=https;branch=master;destsuffix=pico-sdk \
    git://github.com/raspberrypi/pico-extras.git;name=pico-extras;protocol=https;branch=master;destsuffix=pico-extras \
    git://github.com/pimoroni/pimoroni-pico.git;name=pimoroni-pico;protocol=https;branch=main;destsuffix=pimoroni-pico \
    git://github.com/hathach/tinyusb.git;name=tinyusb;protocol=https;branch=master;destsuffix=tinyusb \
"

SRCREV_pico-sdk = "a1438dff1d38bd9c65dbd693f0e5db4b9ae91779"
SRCREV_pico-extras = "82409a94de00802105c84e5c06f333114bb8b316"
SRCREV_pimoroni-pico = "1e7fb9e723c18fea24aa9353e767cadee2a87d70"
SRCREV_tinyusb = "3af1bec1a9161ee8dec29487831f7ac7ade9e189"
SRCREV_FORMAT = "pico-sdk_pico-extras_pimoroni-pico_tinyusb"

S = "${WORKDIR}/picade-max-audio"
B = "${WORKDIR}/build"

DEPENDS += "cmake-native ninja-native"

# Do not use cmake.bbclass here.
# cmake.bbclass injects native build flags (eg -pthread) which break the Pico
# cross toolchain (arm-none-eabi-gcc), causing CMake compiler checks to fail.
do_configure() {
    # Keep Yocto's native build flags out of the Pico toolchain invocation.
    export CFLAGS=""
    export CXXFLAGS=""
    export CPPFLAGS=""
    export LDFLAGS=""

    cmake -S ${S} -B ${B} -G Ninja \
        -DCMAKE_EXE_LINKER_FLAGS= \
        -DCMAKE_MODULE_LINKER_FLAGS= \
        -DCMAKE_SHARED_LINKER_FLAGS= \
        -DCMAKE_STATIC_LINKER_FLAGS= \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DPICO_SDK_PATH=${WORKDIR}/pico-sdk \
        -DPICO_TINYUSB_PATH=${WORKDIR}/tinyusb \
        -DPICO_EXTRAS_PATH=${WORKDIR}/pico-extras \
        -DPIMORONI_PICO_PATH=${WORKDIR}/pimoroni-pico \
        -DPICO_COMPILER=pico_arm_cortex_m0plus_gcc \
        -DPICO_TOOLCHAIN_PATH=${HOSTTOOLS_DIR}
}

do_compile() {
    ninja -C ${B}
}

do_install() {
    install -d ${D}${datadir}/dirtsim/firmware
    install -m 0644 ${B}/picade-max-audio.uf2 ${D}${datadir}/dirtsim/firmware/picade-max-audio.uf2
}

FILES:${PN} += "${datadir}/dirtsim/firmware/picade-max-audio.uf2"

# Build the UF2 in a native task environment (not aarch64), then package it into the image.
BBCLASSEXTEND = "native"
