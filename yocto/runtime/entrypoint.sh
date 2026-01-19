#!/bin/sh
set -e

if [ -z "${DIRTSIM_WORKDIR:-}" ]; then
  DIRTSIM_WORKDIR="/data/dirtsim"
  export DIRTSIM_WORKDIR
fi

mkdir -p "$DIRTSIM_WORKDIR"

if [ -z "${DISPLAY:-}" ]; then
  DISPLAY="${DIRTSIM_UI_DISPLAY:-:99}"
  export DISPLAY
fi

if [ -z "${DIRTSIM_UI_DISPLAY:-}" ]; then
  DIRTSIM_UI_DISPLAY="$DISPLAY"
  export DIRTSIM_UI_DISPLAY
fi

if [ -z "${DIRTSIM_DISABLE_XVFB:-}" ]; then
  if ! command -v Xvfb >/dev/null 2>&1; then
    echo "Xvfb not found in image." >&2
    exit 1
  fi

  XVFB_SCREEN="${DIRTSIM_XVFB_SCREEN:-1280x720x24}"
  Xvfb "$DISPLAY" -screen 0 "$XVFB_SCREEN" -nolisten tcp &
fi

if [ -f /usr/share/dirtsim/fonts/NotoColorEmoji.ttf ] \
  && [ ! -f "$DIRTSIM_WORKDIR/fonts/NotoColorEmoji.ttf" ]; then
  mkdir -p "$DIRTSIM_WORKDIR/fonts"
  cp /usr/share/dirtsim/fonts/NotoColorEmoji.ttf "$DIRTSIM_WORKDIR/fonts/"
fi

exec "$@"
