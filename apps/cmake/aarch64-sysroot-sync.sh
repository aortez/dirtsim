#!/bin/bash
# Sync sysroot from Raspberry Pi for cross-compilation.
#
# This script copies the necessary headers and libraries from the Pi
# to a local sysroot directory for cross-compilation.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SYSROOT_DIR="${PROJECT_DIR}/sysroot-aarch64"

# Configuration - adjust these for your setup.
PI_HOST="${PI_HOST:-dirtsim.local}"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/id_ed25519_sparkle_duck}"

echo "=== Syncing sysroot from ${PI_HOST} ==="
echo "Target: ${SYSROOT_DIR}"

# SSH options.
SSH_OPTS="-o BatchMode=yes -o StrictHostKeyChecking=accept-new"
if [[ -f "$SSH_KEY" ]]; then
    SSH_OPTS="$SSH_OPTS -i $SSH_KEY"
fi

# Create sysroot directory structure.
mkdir -p "${SYSROOT_DIR}"

# Directories to sync from the Pi.
# We need: headers, libraries, and pkg-config files.
SYNC_PATHS=(
    "/usr/include"
    "/usr/lib/aarch64-linux-gnu"
    "/lib/aarch64-linux-gnu"
    "/usr/share/pkgconfig"
    "/usr/lib/pkgconfig"
)

echo ""
echo "Syncing directories..."

for path in "${SYNC_PATHS[@]}"; do
    echo "  ${path}"
    # Create parent directory structure.
    mkdir -p "${SYSROOT_DIR}$(dirname "$path")"

    # Use rsync with compression and relative paths.
    rsync -az --info=progress2 \
        -e "ssh ${SSH_OPTS}" \
        --include='*.h' \
        --include='*.hpp' \
        --include='*.so*' \
        --include='*.a' \
        --include='*.pc' \
        --include='*.cmake' \
        --include='*/' \
        --exclude='*.o' \
        --exclude='*.pyc' \
        "${PI_HOST}:${path}/" "${SYSROOT_DIR}${path}/" 2>/dev/null || {
            echo "    (skipped - may not exist on Pi)"
        }
done

# Also sync some critical files from /lib.
echo "  /lib (critical runtime files)"
mkdir -p "${SYSROOT_DIR}/lib"
rsync -az --info=progress2 \
    -e "ssh ${SSH_OPTS}" \
    --include='ld-linux-aarch64.so*' \
    --include='libc.so*' \
    --include='libm.so*' \
    --include='libpthread.so*' \
    --include='libdl.so*' \
    --include='*/' \
    --exclude='*' \
    "${PI_HOST}:/lib/aarch64-linux-gnu/" "${SYSROOT_DIR}/lib/aarch64-linux-gnu/" 2>/dev/null || true

# Fix symlinks that point to absolute paths.
echo ""
echo "Fixing absolute symlinks..."
find "${SYSROOT_DIR}" -type l | while read link; do
    target=$(readlink "$link")
    if [[ "$target" == /* ]]; then
        # Absolute symlink - make it relative to sysroot.
        new_target="${SYSROOT_DIR}${target}"
        if [[ -e "$new_target" ]]; then
            # Calculate relative path.
            link_dir=$(dirname "$link")
            rel_target=$(realpath --relative-to="$link_dir" "$new_target" 2>/dev/null) || continue
            ln -sf "$rel_target" "$link"
        fi
    fi
done

echo ""
echo "=== Sysroot sync complete ==="
echo "Sysroot: ${SYSROOT_DIR}"
echo ""
echo "To use with CMake:"
echo "  cmake -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake -B build-aarch64 ."
