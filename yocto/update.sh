#!/bin/bash
# Build and flash the dirtsim Yocto image.
#
# Usage:
#   ./update.sh              # Build and flash (interactive device selection)
#   ./update.sh --build-only # Just build, don't flash
#   ./update.sh --flash-only # Just flash (skip build)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_ONLY=false
FLASH_ONLY=false

# Parse arguments.
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-only)
            BUILD_ONLY=true
            shift
            ;;
        --flash-only)
            FLASH_ONLY=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--build-only|--flash-only]"
            echo ""
            echo "Options:"
            echo "  --build-only  Build the image but don't flash"
            echo "  --flash-only  Flash existing image without rebuilding"
            echo "  -h, --help    Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Build phase.
if [ "$FLASH_ONLY" = false ]; then
    echo ""
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║  Building dirtsim-image...                                    ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo ""

    kas build kas-dirtsim.yml

    echo ""
    echo "✓ Build complete!"
fi

# Flash phase.
if [ "$BUILD_ONLY" = false ]; then
    echo ""
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║  Flashing image...                                            ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo ""

    npm run flash

    echo ""
    echo "✓ All done! Boot the Pi and ssh dirtsim to connect."
fi
