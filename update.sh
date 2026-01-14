#!/bin/bash
# Update script: Build yocto image and deploy to Pi via A/B update.
#
# This is a convenience wrapper around the yocto YOLO update process.
# It builds the image (unless --skip-build) and flashes it to the
# inactive partition on the Pi over the network.
#
# Usage:
#   ./update.sh                     # Build + flash + reboot (no prompts)
#   ./update.sh --skip-build        # Flash existing image (no rebuild)
#   ./update.sh --clean             # Force rebuild (clean image sstate)
#   ./update.sh --clean-all         # Full rebuild (clean server + image)
#   ./update.sh --target 192.168.1.50  # Target specific host
#   ./update.sh --dry-run           # Show what would happen
#   ./update.sh --help              # Show all options
#
# Note: --hold-my-mead is passed by default (skips "type yolo" confirmation).
#
# Prerequisites:
#   - Pi must be accessible via SSH at dirtsim.local (or --target host)
#   - SSH key must be configured (run: cd yocto && npm run flash -- --reconfigure)
#   - kas tool installed (pip3 install kas)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YOCTO_DIR="$SCRIPT_DIR/yocto"

# Verify yocto directory exists.
if [ ! -d "$YOCTO_DIR" ]; then
    echo "Error: yocto directory not found at $YOCTO_DIR"
    exit 1
fi

# Verify node_modules exist.
if [ ! -d "$YOCTO_DIR/node_modules" ]; then
    echo "Installing npm dependencies..."
    (cd "$YOCTO_DIR" && npm install)
fi

# Run yolo update with --hold-my-mead by default (skip "type yolo" prompt).
cd "$YOCTO_DIR"
npm run yolo -- --hold-my-mead "$@"
