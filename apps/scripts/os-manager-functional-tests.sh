#!/bin/sh
set -e

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
DEFAULT_BIN_DIR="${REPO_ROOT}/apps/build-debug/bin"

OS_MANAGER_BIN="${DIRTSIM_OS_MANAGER_BIN:-${DEFAULT_BIN_DIR}/dirtsim-os-manager}"
CLI_BIN="${DIRTSIM_CLI_BIN:-${DEFAULT_BIN_DIR}/cli}"
OS_MANAGER_ADDRESS="${DIRTSIM_OS_MANAGER_ADDRESS:-ws://localhost:9090}"
SERVER_ADDRESS="${DIRTSIM_SERVER_ADDRESS:-ws://localhost:8080}"
UI_ADDRESS="${DIRTSIM_UI_ADDRESS:-ws://localhost:7070}"
TIMEOUT_SEC="${DIRTSIM_INTEGRATION_TIMEOUT_SEC:-30}"
TEST_TIMEOUT_MS="${DIRTSIM_FUNCTIONAL_TIMEOUT_MS:-15000}"
DEFAULT_CONFIG_DIR="${REPO_ROOT}/apps/config"

if [ -z "${DIRTSIM_SERVER_ARGS:-}" ]; then
  DIRTSIM_SERVER_ARGS="-p 8080 --config-dir ${DEFAULT_CONFIG_DIR}"
  export DIRTSIM_SERVER_ARGS
fi

if [ ! -e /lib64/ld-linux-x86-64.so.2 ] && [ -e /lib/ld-linux-x86-64.so.2 ]; then
  mkdir -p /lib64
  ln -sf /lib/ld-linux-x86-64.so.2 /lib64/ld-linux-x86-64.so.2
fi
if [ ! -e /lib/libyuv.so.0 ] && [ -e /lib/libyuv.so ]; then
  ln -sf /lib/libyuv.so /lib/libyuv.so.0
fi
if [ ! -e /usr/lib/libyuv.so.0 ] && [ -e /usr/lib/libyuv.so ]; then
  ln -sf /usr/lib/libyuv.so /usr/lib/libyuv.so.0
fi

if [ ! -x "$OS_MANAGER_BIN" ]; then
  echo "os-manager not found or not executable at $OS_MANAGER_BIN" >&2
  exit 1
fi

if [ ! -x "$CLI_BIN" ]; then
  echo "cli not found or not executable at $CLI_BIN" >&2
  exit 1
fi

"$OS_MANAGER_BIN" -p 9090 --backend local &
OS_MANAGER_PID=$!

cleanup() {
  "$CLI_BIN" --address "$OS_MANAGER_ADDRESS" os-manager StopUi >/dev/null 2>&1 || true
  "$CLI_BIN" --address "$OS_MANAGER_ADDRESS" os-manager StopServer >/dev/null 2>&1 || true
  if [ -n "$OS_MANAGER_PID" ]; then
    kill "$OS_MANAGER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

wait_for_os_manager() {
  start_time=$(date +%s)
  while true; do
    if "$CLI_BIN" --address "$OS_MANAGER_ADDRESS" os-manager SystemStatus >/dev/null 2>&1; then
      return 0
    fi

    now=$(date +%s)
    if [ $((now - start_time)) -ge "$TIMEOUT_SEC" ]; then
      echo "Timed out waiting for os-manager at $OS_MANAGER_ADDRESS" >&2
      return 1
    fi
    sleep 0.2
  done
}

TEST_RESULTS=""
TEST_FAILED=0

run_test() {
  test_name="$1"
  timeout_ms="${2:-$TEST_TIMEOUT_MS}"
  echo "Running functional test: $test_name"

  set +e
  result=$("$CLI_BIN" functional-test "$test_name" \
    --timeout "$timeout_ms" \
    --ui-address "$UI_ADDRESS" \
    --server-address "$SERVER_ADDRESS" \
    --os-manager-address "$OS_MANAGER_ADDRESS" 2>&1)
  exit_code=$?
  set -e

  # Parse JSON output for duration and success.
  duration_ms=$(echo "$result" | grep -o '"duration_ms":[0-9]*' | cut -d: -f2 || true)
  if [ -z "$duration_ms" ]; then
    duration_ms=0
  fi
  duration_s=$(awk "BEGIN {printf \"%.1f\", $duration_ms/1000}")

  if [ $exit_code -eq 0 ]; then
    status="✅ Pass"
    echo "  $test_name: PASSED (${duration_s}s)"
  else
    status="❌ Fail"
    TEST_FAILED=1
    echo "  $test_name: FAILED (${duration_s}s)"
    printf "%s\n" "$result"
    # Print error details.
    error_msg=$(echo "$result" | grep -o '"error":"[^"]*"' | cut -d: -f2- | tr -d '"' || echo "")
    if [ -n "$error_msg" ]; then
      echo "    Error: $error_msg"
    fi
  fi

  TEST_RESULTS="${TEST_RESULTS}| ${test_name} | ${status} | ${duration_s}s |\n"
}

wait_for_os_manager

run_test canExit
run_test canTrain
run_test canSetGenerationsAndTrain
run_test canPlantTreeSeed

# Output markdown summary for GitHub Actions.
echo ""
echo "### Functional Tests"
echo ""
echo "| Test | Status | Duration |"
echo "|------|--------|----------|"
printf "$TEST_RESULTS"
echo ""

if [ $TEST_FAILED -eq 1 ]; then
  echo "os-manager functional tests FAILED"
  exit 1
fi

echo "os-manager functional tests PASSED"
