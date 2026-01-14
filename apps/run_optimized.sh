#!/bin/bash
set -e

# Check if server or UI are already running.
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

# Build release version.
echo "Building release version..."
if ! make release; then
    echo "Build failed!"
    exit 1
fi

echo "Build succeeded!"
echo ""

# Run CLI with run-all command (launches server + UI, handles cleanup).
./build-release/bin/cli run-all
