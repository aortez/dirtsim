#!/bin/bash
set -e

# Default log level.
LOG_LEVEL=""

# Parse command line arguments.
while [[ $# -gt 0 ]]; do
    case $1 in
        -l|--log-level)
            LOG_LEVEL="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -l, --log-level LEVEL    Set log level (trace, debug, info, warn, error, critical, off)"
            echo "                           Default: info (built into apps)"
            echo "  -h, --help              Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                      # Run with default info level"
            echo "  $0 -l debug             # Run with debug logging"
            echo "  $0 --log-level trace    # Run with trace logging"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use -h or --help for usage information"
            exit 1
            ;;
    esac
done

# Check if our debug binaries are already running.
# Use full path to avoid matching release builds or other processes.
if pgrep -f "build-debug/bin/dirtsim-server" > /dev/null; then
    echo "Error: dirtsim-server (debug) is already running"
    echo "Kill it with: pkill -f 'build-debug/bin/dirtsim-server'"
    exit 1
fi

if pgrep -f "build-debug/bin/dirtsim-ui" > /dev/null; then
    echo "Error: dirtsim-ui (debug) is already running"
    echo "Kill it with: pkill -f 'build-debug/bin/dirtsim-ui'"
    exit 1
fi

# Build debug version.
echo "Building debug version..."
if ! make debug; then
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

# Function to clean up on exit.
cleanup() {
    echo ""
    echo "Shutting down..."
    # Kill UI first (it's usually in foreground).
    pkill -f "build-debug/bin/dirtsim-ui" 2>/dev/null || true
    # Kill server.
    pkill -f "build-debug/bin/dirtsim-server" 2>/dev/null || true
    echo "Cleanup complete"
}

# Set up trap for cleanup on exit.
trap cleanup EXIT INT TERM

# Launch server in background.
echo "Launching DSSM server on port 8080..."
./build-debug/bin/dirtsim-server $LOG_ARGS -p 8080 &
SERVER_PID=$!

# Wait a moment for server to start.
sleep 1

# Check if server is still running.
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server failed to start!"
    exit 1
fi

echo "Server is ready"
echo ""

# Launch UI in foreground (so Ctrl-C works naturally).
echo "Launching UI (auto-detecting display backend)..."
echo ""
echo "=== Both server and UI are running ==="
echo "Server: ws://localhost:8080"
echo "UI:     ws://localhost:7070"
echo ""

# Run UI in foreground - when it exits, cleanup will run.
# Backend is auto-detected from XDG_SESSION_TYPE / WAYLAND_DISPLAY.
# Use -b to override if needed (e.g., -b x11 or -b wayland).
./build-debug/bin/dirtsim-ui $LOG_ARGS --connect localhost:8080

