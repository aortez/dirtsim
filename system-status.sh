#!/bin/bash
# Get the system status JSON from a remote dirtsim device.
#
# Usage:
#   ./system-status.sh [hostname]
#
# Default hostname: dirtsim.local

set -e

REMOTE_HOST="${1:-dirtsim.local}"

ssh "${REMOTE_HOST}" "dirtsim-cli os-manager SystemStatus"
