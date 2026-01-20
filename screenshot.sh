#!/bin/bash
# Takes a screenshot from a remote dirtsim device, writes it to screenshot.png, and copies it to your clipboard.
# Also copies the systemd logs from the dirtsim services to a local file.
set -xeuo pipefail

HOST="${1:-dirtsim2.local}"
REMOTE_FILE="/tmp/screenshot.png"
LOCAL_FILE="screenshot.png"
LOG_FILE="${HOST}.log"

# Take screenshot on remote device using the CLI.
ssh "$HOST" "dirtsim-cli screenshot $REMOTE_FILE"

# Copy the screenshot back.
scp "$HOST:$REMOTE_FILE" "$LOCAL_FILE"

# Copy the dirtsim service logs.
ssh "$HOST" "journalctl -u 'dirtsim-*' --no-pager" > "$LOG_FILE"
echo "Logs saved to $LOG_FILE"

# Copy to clipboard.
copy_to_clipboard() {
    if [[ -n "${WAYLAND_DISPLAY-}" || -n "${SWAYSOCK-}" ]]; then
        if command -v wl-copy >/dev/null 2>&1; then
            wl-copy --type image/png < "$LOCAL_FILE"
            return 0
        fi
        echo "wl-copy not found; falling back to xclip." >&2
    fi

    if command -v xclip >/dev/null 2>&1; then
        xclip -selection clipboard -t image/png -i "$LOCAL_FILE"
        return 0
    fi

    echo "No clipboard tool found (wl-copy or xclip)." >&2
    return 1
}

copy_to_clipboard

echo "Screenshot saved to $LOCAL_FILE and copied to clipboard. Logs saved to $LOG_FILE."
