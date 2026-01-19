SUMMARY = "DirtSim x86_64 runtime image"
DESCRIPTION = "Yocto rootfs image for running DirtSim in a container on x86_64."
LICENSE = "MIT"

inherit core-image

IMAGE_INSTALL:append = " \
    bash \
    coreutils \
    dirtsim-os-manager \
    dirtsim-server \
    dirtsim-ui \
    xkbcomp \
    xkeyboard-config \
    xauth \
    xserver-xorg-xvfb \
"

setup_data_dir() {
    install -d -m 0755 ${IMAGE_ROOTFS}/data/dirtsim
    install -d -m 0755 ${IMAGE_ROOTFS}/data/dirtsim/logs
}
ROOTFS_POSTPROCESS_COMMAND:append = " setup_data_dir; "
