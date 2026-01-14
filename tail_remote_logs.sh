#!/bin/bash
# Tail logs from remote dirtsim Pi.
#
# Usage:
#   ./tail_remote_logs.sh [hostname]
#
# Default hostname: dirtsim.local

set -e

# Configuration.
REMOTE_HOST="${1:-dirtsim.local}"
SERVICES="dirtsim-server.service dirtsim-ui.service"

# Colors.
COLOR_RESET='\033[0m'
COLOR_CYAN='\033[36m'
COLOR_YELLOW='\033[33m'

echo -e "${COLOR_CYAN}Tailing logs from ${REMOTE_HOST}${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Services: ${SERVICES}${COLOR_RESET}"
echo ""

# Tail logs from remote Pi.
ssh "${REMOTE_HOST}" "sudo journalctl -u dirtsim-server.service -u dirtsim-ui.service -f --no-pager"
