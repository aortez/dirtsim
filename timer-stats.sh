#!/bin/bash
# Fetch and print sorted server timer stats from a DirtSim host.
#
# Usage:
#   ./timer-stats.sh [hostname] [sort]
#
# Sort keys:
#   pct   - sort by % of advance_time descending (default).
#   avg   - sort by avg_ms descending.
#   total - sort by total_ms descending.
#   calls - sort by call count descending.
#   name  - sort alphabetically by timer name.
#
# Default hostname: dirtsim.local

set -euo pipefail

REMOTE_HOST="${1:-dirtsim.local}"
SORT_BY="${2:-pct}"

case "$SORT_BY" in
    pct|avg|total|calls|name)
        ;;
    *)
        echo "Usage: $0 [hostname] [pct|avg|total|calls|name]" >&2
        exit 1
        ;;
esac

if ! command -v jq >/dev/null 2>&1; then
    echo "Error: jq is required but not installed." >&2
    exit 1
fi

JSON_OUTPUT="$(
    ssh "$REMOTE_HOST" "dirtsim-cli server TimerStatsGet 2>/dev/null" \
        | awk '/^\{/{print; exit}'
)"

if [[ -z "$JSON_OUTPUT" ]]; then
    echo "Error: failed to parse TimerStatsGet response from ${REMOTE_HOST}." >&2
    exit 1
fi

echo "Server timer stats for ${REMOTE_HOST} (sorted by ${SORT_BY})"
echo

jq -r '
    .value as $timers
    | [
        ["training_total", $timers.training_total.avg_ms, $timers.training_total.total_ms, $timers.training_total.calls],
        ["total_simulation", $timers.total_simulation.avg_ms, $timers.total_simulation.total_ms, $timers.total_simulation.calls],
        ["advance_time", $timers.advance_time.avg_ms, $timers.advance_time.total_ms, $timers.advance_time.calls]
    ]
    | .[]
    | @tsv
' <<<"$JSON_OUTPUT" | awk -F $'\t' '
    BEGIN {
        print "summary";
        printf "%-20s %12s %14s %12s\n", "timer", "avg_ms", "total_ms", "calls";
    }
    {
        printf "%-20s %12.6f %14.3f %12d\n", $1, $2, $3, $4;
    }
'

echo

jq -r --arg sort "$SORT_BY" '
    .value as $timers
    | $timers.advance_time.total_ms as $advance_total_ms
    | $timers
    | to_entries
    | map(select(.key != "training_total" and .key != "total_simulation" and .key != "advance_time"))
    | sort_by(
        if $sort == "name" then
            .key
        elif $sort == "pct" then
            (.value.total_ms / $advance_total_ms)
        elif $sort == "calls" then
            .value.calls
        elif $sort == "total" then
            .value.total_ms
        else
            .value.avg_ms
        end
    )
    | if $sort == "name" then . else reverse end
    | .[]
    | [
        .key,
        .value.avg_ms,
        .value.total_ms,
        .value.calls,
        (.value.total_ms / $advance_total_ms * 100.0)
    ]
    | @tsv
' <<<"$JSON_OUTPUT" | awk -F $'\t' '
    BEGIN {
        print "breakdown";
        printf "%-40s %12s %14s %12s %10s\n", "timer", "avg_ms", "total_ms", "calls", "%adv";
    }
    {
        printf "%-40s %12.6f %14.3f %12d %9.2f%%\n", $1, $2, $3, $4, $5;
    }
'
