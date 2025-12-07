#!/bin/bash
# Setup script for Sparkle Duck - Installs required dependencies
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse arguments
INSTALL_CROSS=false
for arg in "$@"; do
    case $arg in
        --cross)
            INSTALL_CROSS=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --cross    Install aarch64 cross-compilation toolchain"
            echo "  --help     Show this help message"
            exit 0
            ;;
    esac
done

echo -e "${BLUE}=== Sparkle Duck Dependency Setup ===${NC}\n"
if [ "$INSTALL_CROSS" = true ]; then
    echo -e "${YELLOW}Including cross-compilation toolchain (aarch64)${NC}\n"
fi

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    OS_VERSION=$VERSION_ID
else
    echo -e "${RED}Cannot detect OS. /etc/os-release not found.${NC}"
    exit 1
fi

echo -e "${GREEN}Detected OS: $OS $OS_VERSION${NC}\n"

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to check if a package is installed (Debian/Ubuntu)
package_installed_apt() {
    dpkg -l "$1" 2>/dev/null | grep -q "^ii"
}

# Function to check if a package is installed (Arch)
package_installed_pacman() {
    pacman -Q "$1" >/dev/null 2>&1
}

# Function to check if a package is installed (Fedora/RHEL)
package_installed_dnf() {
    rpm -q "$1" >/dev/null 2>&1
}

install_ubuntu_debian() {
    echo -e "${BLUE}Installing dependencies for Ubuntu/Debian...${NC}"

    PACKAGES=(
        "build-essential"
        "cmake"
        "pkg-config"
        "libboost-dev"
        "libssl-dev"
        "libx11-dev"
        "libwayland-dev"
        "libwayland-client0"
        "libwayland-cursor0"
        "libxkbcommon-dev"
        "wayland-protocols"
        "clang-format"
        "git"
    )

    # Add cross-compilation packages if requested.
    if [ "$INSTALL_CROSS" = true ]; then
        PACKAGES+=(
            "gcc-aarch64-linux-gnu"
            "g++-aarch64-linux-gnu"
        )
    fi

    # Check which packages are missing
    MISSING_PACKAGES=()
    for pkg in "${PACKAGES[@]}"; do
        if ! package_installed_apt "$pkg"; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done

    if [ ${#MISSING_PACKAGES[@]} -eq 0 ]; then
        echo -e "${GREEN}All required packages are already installed!${NC}"
    else
        echo -e "${YELLOW}Missing packages: ${MISSING_PACKAGES[*]}${NC}"
        echo -e "${BLUE}Installing missing packages...${NC}"
        sudo apt-get update
        sudo apt-get install -y "${MISSING_PACKAGES[@]}"
        echo -e "${GREEN}Dependencies installed successfully!${NC}"
    fi
}

install_arch() {
    echo -e "${BLUE}Installing dependencies for Arch Linux...${NC}"

    PACKAGES=(
        "base-devel"
        "cmake"
        "pkgconf"
        "boost"
        "openssl"
        "libx11"
        "wayland"
        "wayland-protocols"
        "xkbcommon"
        "clang"
        "git"
    )

    # Add cross-compilation packages if requested.
    if [ "$INSTALL_CROSS" = true ]; then
        PACKAGES+=(
            "aarch64-linux-gnu-gcc"
        )
    fi

    # Check which packages are missing
    MISSING_PACKAGES=()
    for pkg in "${PACKAGES[@]}"; do
        if ! package_installed_pacman "$pkg"; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done

    if [ ${#MISSING_PACKAGES[@]} -eq 0 ]; then
        echo -e "${GREEN}All required packages are already installed!${NC}"
    else
        echo -e "${YELLOW}Missing packages: ${MISSING_PACKAGES[*]}${NC}"
        echo -e "${BLUE}Installing missing packages...${NC}"
        sudo pacman -S --needed --noconfirm "${MISSING_PACKAGES[@]}"
        echo -e "${GREEN}Dependencies installed successfully!${NC}"
    fi
}

install_fedora_rhel() {
    echo -e "${BLUE}Installing dependencies for Fedora/RHEL...${NC}"

    PACKAGES=(
        "gcc-c++"
        "cmake"
        "pkgconfig"
        "boost-devel"
        "openssl-devel"
        "libX11-devel"
        "wayland-devel"
        "wayland-protocols-devel"
        "libxkbcommon-devel"
        "clang-tools-extra"
        "git"
    )

    # Add cross-compilation packages if requested.
    if [ "$INSTALL_CROSS" = true ]; then
        PACKAGES+=(
            "gcc-aarch64-linux-gnu"
            "gcc-c++-aarch64-linux-gnu"
        )
    fi

    # Check which packages are missing
    MISSING_PACKAGES=()
    for pkg in "${PACKAGES[@]}"; do
        if ! package_installed_dnf "$pkg"; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done

    if [ ${#MISSING_PACKAGES[@]} -eq 0 ]; then
        echo -e "${GREEN}All required packages are already installed!${NC}"
    else
        echo -e "${YELLOW}Missing packages: ${MISSING_PACKAGES[*]}${NC}"
        echo -e "${BLUE}Installing missing packages...${NC}"
        sudo dnf install -y "${MISSING_PACKAGES[@]}"
        echo -e "${GREEN}Dependencies installed successfully!${NC}"
    fi
}

# Install based on detected OS
case "$OS" in
    ubuntu|debian|linuxmint|pop)
        install_ubuntu_debian
        ;;
    arch|manjaro|endeavouros)
        install_arch
        ;;
    fedora|rhel|centos|rocky|almalinux)
        install_fedora_rhel
        ;;
    *)
        echo -e "${RED}Unsupported OS: $OS${NC}"
        echo -e "${YELLOW}Please install dependencies manually. See README.md for package list.${NC}"
        exit 1
        ;;
esac

# Verify installation
echo -e "\n${BLUE}=== Verifying installation ===${NC}"

REQUIRED_COMMANDS=("cmake" "pkg-config" "make" "g++" "git")
ALL_OK=true

for cmd in "${REQUIRED_COMMANDS[@]}"; do
    if command_exists "$cmd"; then
        VERSION=$($cmd --version 2>/dev/null | head -n1 || echo "version unknown")
        echo -e "${GREEN}✓${NC} $cmd: $VERSION"
    else
        echo -e "${RED}✗${NC} $cmd: NOT FOUND"
        ALL_OK=false
    fi
done

# Check pkg-config libraries
echo -e "\n${BLUE}Checking pkg-config libraries...${NC}"
REQUIRED_LIBS=("x11" "wayland-client" "xkbcommon")

for lib in "${REQUIRED_LIBS[@]}"; do
    if pkg-config --exists "$lib"; then
        VERSION=$(pkg-config --modversion "$lib")
        echo -e "${GREEN}✓${NC} $lib: $VERSION"
    else
        echo -e "${YELLOW}⚠${NC} $lib: NOT FOUND (may still work if statically linked)"
    fi
done

# Check cross-compiler if requested.
if [ "$INSTALL_CROSS" = true ]; then
    echo -e "\n${BLUE}Checking cross-compilation toolchain...${NC}"
    if command_exists "aarch64-linux-gnu-g++"; then
        VERSION=$(aarch64-linux-gnu-g++ --version 2>/dev/null | head -n1 || echo "version unknown")
        echo -e "${GREEN}✓${NC} aarch64-linux-gnu-g++: $VERSION"
    else
        echo -e "${RED}✗${NC} aarch64-linux-gnu-g++: NOT FOUND"
        ALL_OK=false
    fi
fi

echo -e "\n${BLUE}=== Setup Complete ===${NC}"

if [ "$ALL_OK" = true ]; then
    echo -e "${GREEN}All required tools are installed!${NC}"
    echo -e "\nNext steps:"
    echo -e "  1. Initialize submodules: ${YELLOW}git submodule update --init --recursive${NC}"
    echo -e "  2. Build the project:     ${YELLOW}make debug${NC}"
    echo -e "  3. Run tests:             ${YELLOW}make test${NC}"
    echo -e "  4. Run the application:   ${YELLOW}./build-debug/bin/sparkle-duck-ui -b wayland${NC}"
    if [ "$INSTALL_CROSS" = true ]; then
        echo -e "\nCross-compilation:"
        echo -e "  5. Sync sysroot from Pi:  ${YELLOW}make cross-sysroot${NC}"
        echo -e "  6. Cross-compile:         ${YELLOW}make cross-release${NC}"
        echo -e "  7. Deploy to Pi:          ${YELLOW}./deploy-to-pi.sh -x${NC}"
    fi
    echo -e "\nFor more information, see README.md"
else
    echo -e "${RED}Some required tools are missing. Please install them manually.${NC}"
    exit 1
fi
