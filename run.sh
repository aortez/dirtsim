#!/bin/bash
set -e

# Defaults.
LOG_LEVEL=""
BUILD_TYPE="debug"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPS_DIR="$REPO_ROOT/apps"
CONFIG_DIR="$APPS_DIR/config"

# Parse command line arguments.
while [[ $# -gt 0 ]]; do
    case $1 in
        -l|--log-level)
            LOG_LEVEL="$2"
            shift 2
            ;;
        -r|--release)
            BUILD_TYPE="release"
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="debug"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -l, --log-level LEVEL    Set log level (trace, debug, info, warn, error, critical, off)"
            echo "                           Default: info (built into apps)"
            echo "  -r, --release            Build and run release mode"
            echo "  -d, --debug              Build and run debug mode (default)"
            echo "  -h, --help              Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                      # Run with default info level"
            echo "  $0 -l debug             # Run with debug logging"
            echo "  $0 --log-level trace    # Run with trace logging"
            echo "  $0 --release            # Build and run release mode"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use -h or --help for usage information"
            exit 1
            ;;
    esac
done

BUILD_DIR="$APPS_DIR/build-$BUILD_TYPE"
BIN_DIR="$BUILD_DIR/bin"
SERVER_MATCH="build-$BUILD_TYPE/bin/dirtsim-server"
UI_MATCH="build-$BUILD_TYPE/bin/dirtsim-ui"
AUDIO_MATCH="build-$BUILD_TYPE/bin/dirtsim-audio"

# Check if our binaries are already running.
# Match the binary path to avoid other processes.
if pgrep -f "$SERVER_MATCH" > /dev/null; then
    echo "Error: dirtsim-server ($BUILD_TYPE) is already running"
    echo "Kill it with: pkill -f '$SERVER_MATCH'"
    exit 1
fi

if pgrep -f "$UI_MATCH" > /dev/null; then
    echo "Error: dirtsim-ui ($BUILD_TYPE) is already running"
    echo "Kill it with: pkill -f '$UI_MATCH'"
    exit 1
fi

if pgrep -f "$AUDIO_MATCH" > /dev/null; then
    echo "Error: dirtsim-audio ($BUILD_TYPE) is already running"
    echo "Kill it with: pkill -f '$AUDIO_MATCH'"
    exit 1
fi

# Build version.
echo "Building $BUILD_TYPE version..."
if ! make -C "$APPS_DIR" "$BUILD_TYPE"; then
    echo "Build failed!"
    exit 1
fi

echo "Build succeeded!"
echo ""

# Prepare log level arguments if specified.
LOG_ARGS=""
if [ -n "$LOG_LEVEL" ]; then
    LOG_ARGS="--log-level $LOG_LEVEL"
    echo "Starting with log level: $LOG_LEVEL"
fi

LOG_CONFIG="$CONFIG_DIR/logging-config.json.local"
LOG_CONFIG_ARGS="--log-config $LOG_CONFIG"

if [ ! -d "$BIN_DIR" ]; then
    echo "Build output not found at: $BIN_DIR"
    exit 1
fi

# Function to clean up on exit.
cleanup() {
    echo ""
    echo "Shutting down..."
    # Kill UI first (it's usually in foreground).
    pkill -f "$UI_MATCH" 2>/dev/null || true
    # Kill audio.
    pkill -f "$AUDIO_MATCH" 2>/dev/null || true
    # Kill server.
    pkill -f "$SERVER_MATCH" 2>/dev/null || true
    echo "Cleanup complete"
}

# Set up trap for cleanup on exit.
trap cleanup EXIT INT TERM

# Launch server in background.
cd "$APPS_DIR"
mkdir -p "$CONFIG_DIR"
echo "Launching DSSM server on port 8080..."
"$BIN_DIR/dirtsim-server" $LOG_ARGS $LOG_CONFIG_ARGS -p 8080 --config-dir "$CONFIG_DIR" &
SERVER_PID=$!

# Wait a moment for server to start.
sleep 0.1

# Check if server is still running.
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server failed to start!"
    exit 1
fi

echo "Server is ready"
echo ""

# Launch audio in background.
echo "Launching audio on port 6060..."
"$BIN_DIR/dirtsim-audio" $LOG_ARGS $LOG_CONFIG_ARGS -p 6060 &
AUDIO_PID=$!

# Wait a moment for audio to start.
sleep 0.1

# Check if audio is still running.
if ! kill -0 $AUDIO_PID 2>/dev/null; then
    echo "Audio failed to start!"
    exit 1
fi

echo "Audio is ready"
echo ""

# Launch UI in foreground (so Ctrl-C works naturally).
echo "Launching UI (auto-detecting display backend)..."
echo ""
echo "=== Server, UI, and audio are running ==="
echo "Server: ws://localhost:8080"
echo "UI:     ws://localhost:7070"
echo "Audio:  ws://localhost:6060"
echo ""

# Run UI in foreground - when it exits, cleanup will run.
# Backend is auto-detected from XDG_SESSION_TYPE / WAYLAND_DISPLAY.
# Use -b to override if needed (e.g., -b x11 or -b wayland).
"$BIN_DIR/dirtsim-ui" $LOG_ARGS $LOG_CONFIG_ARGS --connect localhost:8080
