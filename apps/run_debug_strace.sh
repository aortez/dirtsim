#!/bin/bash
# Wrapper to run debug with strace on UI process
set -e

# Parse log level
LOG_LEVEL=""
while [[ $# -gt 0 ]]; do
    case $1 in
        -l|--log-level)
            LOG_LEVEL="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

# Clean log
rm -f dirtsim.log

# Check if already running
if pgrep -f "dirtsim-server" > /dev/null; then
    echo "Error: dirtsim-server is already running"
    echo "Kill it with: pkill -f dirtsim-server"
    exit 1
fi

if pgrep -f "dirtsim-ui" > /dev/null; then
    echo "Error: dirtsim-ui is already running"
    echo "Kill it with: pkill -f dirtsim-ui"
    exit 1
fi

# Build
echo "Building debug version..."
if ! make debug; then
    echo "Build failed!"
    exit 1
fi

echo "Build succeeded!"
echo ""

# Prepare log args
LOG_ARGS=""
if [ -n "$LOG_LEVEL" ]; then
    LOG_ARGS="--log-level $LOG_LEVEL"
    echo "Starting with log level: $LOG_LEVEL"
fi

# Cleanup function
cleanup() {
    echo ""
    echo "Shutting down..."
    pkill -f "dirtsim-ui" 2>/dev/null || true
    pkill -f "dirtsim-server" 2>/dev/null || true
    echo "Cleanup complete"
}

trap cleanup EXIT INT TERM

# Launch server
echo "Launching DSSM server on port 8080..."
./build-debug/bin/dirtsim-server $LOG_ARGS -p 8080 &
SERVER_PID=$!

sleep 1

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server failed to start!"
    exit 1
fi

echo "Server is ready"
echo ""

# Launch UI with strace
echo "Launching UI with strace (watching poll/select/recv)..."
echo ""
echo "=== Strace output will be mixed with UI logs ==="
echo ""

# Run UI under strace - focus on blocking syscalls
strace -e trace=poll,select,recvfrom,recvmsg,futex -tt -T \
    ./build-debug/bin/dirtsim-ui $LOG_ARGS -b wayland --connect localhost:8080 \
    2>&1 | tee strace-ui.log
