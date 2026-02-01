#!/bin/sh
set -e

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/../.." && pwd)
DEFAULT_BIN_DIR="${REPO_ROOT}/apps/build-debug/bin"

OS_MANAGER_BIN="${DIRTSIM_OS_MANAGER_BIN:-${DEFAULT_BIN_DIR}/dirtsim-os-manager}"
CLI_BIN="${DIRTSIM_CLI_BIN:-${DEFAULT_BIN_DIR}/cli}"
DOCS_CLI_BIN="${DIRTSIM_DOCS_SCREENSHOT_CLI_BIN:-}"
OS_MANAGER_ADDRESS="${DIRTSIM_OS_MANAGER_ADDRESS:-ws://localhost:9090}"
SERVER_ADDRESS="${DIRTSIM_SERVER_ADDRESS:-ws://localhost:8080}"
UI_ADDRESS="${DIRTSIM_UI_ADDRESS:-ws://localhost:7070}"
TIMEOUT_SEC="${DIRTSIM_INTEGRATION_TIMEOUT_SEC:-20}"

if [ ! -x "$OS_MANAGER_BIN" ]; then
  echo "os-manager not found or not executable at $OS_MANAGER_BIN" >&2
  exit 1
fi

if [ ! -x "$CLI_BIN" ]; then
  echo "cli not found or not executable at $CLI_BIN" >&2
  exit 1
fi

if [ -z "$DOCS_CLI_BIN" ]; then
  if [ -n "$DIRTSIM_CLI_BIN" ] && [ -x "$DIRTSIM_CLI_BIN" ]; then
    DOCS_CLI_BIN="$DIRTSIM_CLI_BIN"
  elif [ -x "${REPO_ROOT}/apps/build-debug/bin/cli" ]; then
    DOCS_CLI_BIN="${REPO_ROOT}/apps/build-debug/bin/cli"
  else
    DOCS_CLI_BIN="$CLI_BIN"
  fi
fi

if [ ! -x "$DOCS_CLI_BIN" ]; then
  echo "docs-screenshots cli not found or not executable at $DOCS_CLI_BIN" >&2
  exit 1
fi

"$OS_MANAGER_BIN" -p 9090 --backend local &
OS_MANAGER_PID=$!

cleanup() {
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

wait_for_server_ready() {
  start_time=$(date +%s)
  while true; do
    output=$("$CLI_BIN" --address "$SERVER_ADDRESS" server StatusGet 2>/dev/null || true)
    echo "$output" | grep -Eq '"state"[[:space:]]*:[[:space:]]*"Idle"' && return 0
    echo "$output" | grep -Eq '"state"[[:space:]]*:[[:space:]]*"SimRunning"' && return 0

    now=$(date +%s)
    if [ $((now - start_time)) -ge "$TIMEOUT_SEC" ]; then
      echo "Timed out waiting for server at $SERVER_ADDRESS" >&2
      return 1
    fi
    sleep 0.2
  done
}

wait_for_ui_ready() {
  start_time=$(date +%s)
  while true; do
    output=$("$CLI_BIN" --address "$UI_ADDRESS" ui StatusGet 2>/dev/null || true)
    echo "$output" | grep -Eq '"connected_to_server"[[:space:]]*:[[:space:]]*true' && return 0

    now=$(date +%s)
    if [ $((now - start_time)) -ge "$TIMEOUT_SEC" ]; then
      echo "Timed out waiting for UI at $UI_ADDRESS" >&2
      return 1
    fi
    sleep 0.2
  done
}

wait_for_os_manager

"$CLI_BIN" --address "$OS_MANAGER_ADDRESS" os-manager StartServer >/dev/null
wait_for_server_ready

"$CLI_BIN" --address "$OS_MANAGER_ADDRESS" os-manager StartUi >/dev/null
wait_for_ui_ready

if [ "${DIRTSIM_DOCS_SCREENSHOTS:-1}" != "0" ]; then
  DIRTSIM_UI_ADDRESS="$UI_ADDRESS" \
  DIRTSIM_SERVER_ADDRESS="$SERVER_ADDRESS" \
  "$DOCS_CLI_BIN" docs-screenshots
fi

"$CLI_BIN" --address "$SERVER_ADDRESS" server SimRun \
  '{"timestep":0.016,"max_steps":1,"max_frame_ms":0,"start_paused":false}' >/dev/null

"$CLI_BIN" --address "$OS_MANAGER_ADDRESS" os-manager StopUi >/dev/null || true
"$CLI_BIN" --address "$OS_MANAGER_ADDRESS" os-manager StopServer >/dev/null || true

echo "os-manager integration test PASSED"
