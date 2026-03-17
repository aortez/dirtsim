#!/bin/bash
# Capture NES latency logs from a remote dirtsim device and extract the useful lines.
#
# Usage:
#   ./capture_nes_latency_logs.sh [hostname] [label]
#   ./capture_nes_latency_logs.sh --summary <raw-log-path>
#
# Examples:
#   ./capture_nes_latency_logs.sh dirtsim2.local 0ms
#   ./capture_nes_latency_logs.sh --summary /tmp/dirtsim-latency/20260315-183000-0ms.raw.log

set -euo pipefail

SUMMARY_PATTERN='Live NES frame delay config updated|Live NES frame delay config applied|NES frame delay pacing|Live input latency|SMB response latency|UI frame staging|Breakdown:|NES jump setup|NES turn jump outcome|NES jump chord probe|NES jump chord mismatch|Start lateness|Period overrun|Transport delay|Queue delay|Display latency|Frame interval|Frames to SMB input mask|Observed -> SMB input mask|Latch -> SMB input mask|SMB input mask -> response detect|Observed -> request|Observed -> latch|Observed -> display|Request -> latch|Request -> display|Latch -> display|Observed -> response detect|Observed -> response display|Response detect -> display|Frames after latch|Handled phase|Receive -> UI apply|UI apply -> timer start|Timer start -> flush'
DEFAULT_HOST="dirtsim2.local"
OUTPUT_DIR="${TMPDIR:-/tmp}/dirtsim-latency"

print_usage() {
    cat <<EOF
Usage:
  $0 [hostname] [label]
  $0 --summary <raw-log-path>

Capture mode:
  Starts a live journal tail for dirtsim server+ui, saves the raw log, and writes a filtered summary
  when you stop capture with Ctrl-C.

Arguments:
  hostname   Remote host to read logs from. Default: ${DEFAULT_HOST}
  label      Short run label, e.g. 0ms, 2ms, 4ms. Default: run

Summary mode:
  Re-runs the filter against an existing raw log file.
EOF
}

write_summary() {
    local raw_log="$1"
    local summary_log="$2"

    if [ ! -s "${raw_log}" ]; then
        echo "No log data captured in ${raw_log}" >&2
        return 1
    fi

    if ! grep -E "${SUMMARY_PATTERN}" "${raw_log}" > "${summary_log}"; then
        : > "${summary_log}"
    fi

    echo "Raw log: ${raw_log}"
    echo "Summary: ${summary_log}"
    echo
    if [ -s "${summary_log}" ]; then
        cat "${summary_log}"
    else
        echo "No matching latency lines found in ${summary_log}" >&2
    fi

    if ! grep -q 'Live NES frame delay config applied' "${summary_log}"; then
        echo >&2
        echo "Warning: no frame-delay config apply line was captured." >&2
        echo "The run may still be using the previous delay value." >&2
    fi
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    print_usage
    exit 0
fi

mkdir -p "${OUTPUT_DIR}"

if [ "${1:-}" = "--summary" ]; then
    if [ $# -ne 2 ]; then
        print_usage >&2
        exit 1
    fi

    RAW_LOG="$2"
    BASENAME="$(basename "${RAW_LOG}")"
    SUMMARY_LOG="${OUTPUT_DIR}/${BASENAME%.raw.log}.summary.log"
    write_summary "${RAW_LOG}" "${SUMMARY_LOG}"
    exit 0
fi

REMOTE_HOST="${1:-${DEFAULT_HOST}}"
RUN_LABEL="${2:-run}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
RAW_LOG="${OUTPUT_DIR}/${TIMESTAMP}-${RUN_LABEL}.raw.log"
SUMMARY_LOG="${OUTPUT_DIR}/${TIMESTAMP}-${RUN_LABEL}.summary.log"

echo "Capturing logs from ${REMOTE_HOST}"
echo "Run label: ${RUN_LABEL}"
echo "Raw log will be saved to ${RAW_LOG}"
echo
echo "Start SMB and play. Press Ctrl-C here when the run is done."
echo

set +e
ssh "${REMOTE_HOST}" \
    "journalctl -u dirtsim-server.service -u dirtsim-ui.service -n 0 -f --no-pager" \
    | tee "${RAW_LOG}"
CAPTURE_STATUS=$?
set -e

echo
if [ "${CAPTURE_STATUS}" -ne 0 ]; then
    echo "Capture exited with status ${CAPTURE_STATUS}. Writing summary anyway." >&2
fi

write_summary "${RAW_LOG}" "${SUMMARY_LOG}"
