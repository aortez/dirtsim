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

SUMMARY_ROWS="$(
    jq -r '
    .value as $timers
    | ["training_total", "total_simulation", "advance_time"]
    | map(select($timers[.] != null))
    | .[]
    | [., ($timers[.].avg_ms // 0), ($timers[.].total_ms // 0), ($timers[.].calls // 0)]
    | @tsv
    ' <<<"$JSON_OUTPUT"
)"

echo "summary"
if [[ -z "$SUMMARY_ROWS" ]]; then
    echo "(no summary timers reported)"
else
    awk -F $'\t' '
    BEGIN {
        printf "%-20s %12s %14s %12s\n", "timer", "avg_ms", "total_ms", "calls";
    }
    {
        printf "%-20s %12.6f %14.3f %12d\n", $1, $2, $3, $4;
    }
    ' <<<"$SUMMARY_ROWS"
fi

echo

REFERENCE_TIMER="$(
    jq -r '
    .value as $timers
    | ["advance_time", "training_total", "total_simulation"]
    | map(select((($timers[.].total_ms // 0) | tonumber) > 0))
    | .[0] // ""
    ' <<<"$JSON_OUTPUT"
)"
REFERENCE_TOTAL_MS="$(
    jq -r --arg reference_timer "$REFERENCE_TIMER" '
    if $reference_timer == "" then
        0
    else
        (.value[$reference_timer].total_ms // 0)
    end
    ' <<<"$JSON_OUTPUT"
)"
if [[ -n "$REFERENCE_TIMER" ]]; then
    echo "percent basis: ${REFERENCE_TIMER}"
else
    echo "percent basis: unavailable; pct sort falls back to total_ms"
fi

jq -r --arg sort "$SORT_BY" --argjson reference_total_ms "$REFERENCE_TOTAL_MS" '
    .value as $timers
    | $reference_total_ms as $reference_total_ms
    | $timers
    | to_entries
    | map(select(.key != "training_total" and .key != "total_simulation" and .key != "advance_time"))
    | map(select(.value.total_ms != null and .value.calls != null))
    | sort_by(
        if $sort == "name" then
            .key
        elif $sort == "pct" then
            if $reference_total_ms > 0 then
                (.value.total_ms / $reference_total_ms)
            else
                .value.total_ms
            end
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
        (.value.avg_ms // 0),
        (.value.total_ms // 0),
        (.value.calls // 0),
        if $reference_total_ms > 0 then
            ((.value.total_ms / $reference_total_ms * 100.0) | tostring)
        else
            "n/a"
        end
    ]
    | @tsv
' <<<"$JSON_OUTPUT" | awk -F $'\t' '
    BEGIN {
        print "breakdown";
        printf "%-40s %12s %14s %12s %10s\n", "timer", "avg_ms", "total_ms", "calls", "%ref";
    }
    {
        if ($5 == "n/a") {
            pct = "n/a";
        }
        else {
            pct = sprintf("%9.2f%%", $5);
        }
        printf "%-40s %12.6f %14.3f %12d %10s\n", $1, $2, $3, $4, pct;
    }
'
