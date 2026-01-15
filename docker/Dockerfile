# Docker image for building and testing dirtsim.
# Base image with all required build dependencies.
FROM ubuntu:24.04

# Avoid interactive prompts during package installation.
ENV DEBIAN_FRONTEND=noninteractive

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
    libavahi-client-dev \
    libopenh264-dev \
    libfreetype-dev \
    libyuv-dev \
    libsdl2-dev \
    libsqlite3-dev \
    sudo \
    ccache \
    && rm -rf /var/lib/apt/lists/*

# Configure password-free sudo for all users.
RUN echo 'ALL ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

# Set up SSH for remote testing.
RUN apt-get update && apt-get install -y \
    openssh-client \
    && rm -rf /var/lib/apt/lists/*

# Configure git to trust the workspace directory.
RUN git config --global --add safe.directory /workspace

# Set working directory.
WORKDIR /workspace

# Default command.
CMD ["/bin/bash"]
