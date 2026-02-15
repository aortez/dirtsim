#!/bin/bash
# Update script: Build yocto image and deploy to Pi via A/B update.
#
# This is a convenience wrapper around the yocto YOLO update process.
# It builds the image (unless --skip-build) and flashes it to the
# inactive partition on the Pi over the network.
#
# Usage:
#   ./update.sh                     # Build in Docker + flash + reboot (no prompts)
#   ./update.sh --skip-build        # Flash existing image (no rebuild)
#   ./update.sh --clean             # Force rebuild (clean image sstate)
#   ./update.sh --clean-all         # Full rebuild (clean server + image)
#   ./update.sh --target 192.168.1.50  # Target specific host
#   ./update.sh --fast              # Fast local deploy (skips Docker/image build)
#   ./update.sh --dry-run           # Show what would happen
#   ./update.sh --help              # Show all options
#
# Note: --hold-my-mead is passed by default (skips "type yolo" confirmation).
#
# Prerequisites:
#   - Pi must be accessible via SSH at dirtsim.local (or --target host)
#   - SSH key must be configured (run: cd yocto && npm run flash -- --reconfigure)
#   - Docker running locally (default build path)
#   - If Docker is disabled, kas tool installed (pip3 install kas)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YOCTO_DIR="$SCRIPT_DIR/yocto"
TARGET_HOST="dirtsim.local"
DRY_RUN=false

# Parse CLI args for target host and dry-run flag.
args=("$@")
FAST_MODE=false
for ((i=0; i<${#args[@]}; i++)); do
    arg="${args[$i]}"
    case "$arg" in
        --target)
            if (( i + 1 < ${#args[@]} )); then
                TARGET_HOST="${args[$((i + 1))]}"
            fi
            ;;
        --target=*)
            TARGET_HOST="${arg#--target=}"
            ;;
        --fast)
            FAST_MODE=true
            ;;
        --dry-run)
            DRY_RUN=true
            ;;
    esac
done
if [ -z "$TARGET_HOST" ]; then
    TARGET_HOST="dirtsim.local"
fi

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

# Default to Docker-backed builds unless:
#   - caller explicitly set DIRTSIM_YOCTO_DOCKER
#   - fast mode is requested (fast mode runs locally by design)
if [ -z "${DIRTSIM_YOCTO_DOCKER+x}" ] && [ "$FAST_MODE" != true ]; then
    export DIRTSIM_YOCTO_DOCKER=1
fi

# Run yolo update with --hold-my-mead by default (skip "type yolo" prompt).
cd "$YOCTO_DIR"
npm run yolo -- --hold-my-mead "$@"

if [ "$DRY_RUN" = true ]; then
    exit 0
fi

REMOTE_TARGET="dirtsim@${TARGET_HOST}"
echo "Waiting for SSH on ${REMOTE_TARGET} (up to 20s)..."

timeout_sec=20
sleep_sec=2
start_time=$SECONDS
while (( SECONDS - start_time < timeout_sec )); do
    if ssh -o BatchMode=yes -o ConnectTimeout=2 -o ConnectionAttempts=1 -o LogLevel=ERROR \
        "${REMOTE_TARGET}" "echo ok" >/dev/null 2>&1; then
        echo "SSH connected to ${REMOTE_TARGET}"
        exit 0
    fi
    sleep "${sleep_sec}"
done

echo "Error: unable to reach ${REMOTE_TARGET} via SSH after ${timeout_sec}s"
exit 1
