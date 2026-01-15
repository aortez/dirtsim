#!/bin/bash
# Takes a screenshot from a remote dirtsim device, writes it to screenshot.png, and copies it to your clipboard.
set -xeuo pipefail

HOST="${1:-dirtsim2.local}"
REMOTE_FILE="/tmp/screenshot.png"
LOCAL_FILE="screenshot.png"

# Take screenshot on remote device using the CLI.
ssh "$HOST" "dirtsim-cli screenshot $REMOTE_FILE"

# Copy the screenshot back.
scp "$HOST:$REMOTE_FILE" "$LOCAL_FILE"

# Copy to clipboard.
xclip -selection clipboard -t image/png -i "$LOCAL_FILE"

echo "Screenshot saved to $LOCAL_FILE and copied to clipboard."
