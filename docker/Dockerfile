# Docker image for building and testing dirtsim.
# Base image with all required build dependencies.
FROM ubuntu:24.04

# Avoid interactive prompts during package installation.
ENV DEBIAN_FRONTEND=noninteractive

# pipx configuration for installing kas.
ENV PIPX_HOME=/opt/pipx
ENV PIPX_BIN_DIR=/usr/local/bin

# Install build dependencies.
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libboost-dev \
    libssl-dev \
    libx11-dev \
    libwayland-dev \
    libwayland-client0 \
    libwayland-cursor0 \
    libxkbcommon-dev \
    wayland-protocols \
    clang-format \
    git \
    openssh-client \
    libavahi-client-dev \
    libnm-dev \
    libopenh264-dev \
    libfreetype-dev \
    libyuv-dev \
    libsdl2-dev \
    libsqlite3-dev \
    chrpath \
    cpio \
    debianutils \
    diffstat \
    file \
    gawk \
    iputils-ping \
    libacl1 \
    liblz4-tool \
    locales \
    pipx \
    python3 \
    python3-git \
    python3-jinja2 \
    python3-pexpect \
    python3-pip \
    python3-subunit \
    python3-venv \
    socat \
    texinfo \
    unzip \
    wget \
    xz-utils \
    zstd \
    sudo \
    ccache \
    && rm -rf /var/lib/apt/lists/*

RUN pipx install kas

RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

# Configure password-free sudo for all users.
RUN echo 'ALL ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

# Configure git to trust the workspace directory.
RUN git config --global --add safe.directory /workspace

# Set working directory.
WORKDIR /workspace

# Default command.
CMD ["/bin/bash"]
