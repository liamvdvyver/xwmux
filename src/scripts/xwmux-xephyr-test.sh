#!/usr/bin/env bash

set -eu

VERBOSE=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        -v)
            VERBOSE=1
            ;;
        *)
            echo "Usage: $0 [-v]" >&2
            exit 1
            ;;
    esac
    shift
done

ROOT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR="$ROOT_DIR/build"
SCRIPTS_DIR="$ROOT_DIR/src/scripts"
XWMUX_BIN="$BUILD_DIR/xwmux"
CTL_BIN="$BUILD_DIR/xwmux-ctl"
HOOKS_SCRIPT="$SCRIPTS_DIR/xwmux-tmux-conf.sh"
TMUX_CONF="$SCRIPTS_DIR/xwmux-test.tmux.conf"

export PATH="$BUILD_DIR:$SCRIPTS_DIR:$PATH"

if [ ! -x "$XWMUX_BIN" ] || [ ! -x "$CTL_BIN" ]; then
    echo "Missing built binaries in $BUILD_DIR" >&2
    echo "Run: cmake -S $ROOT_DIR -B $BUILD_DIR && cmake --build $BUILD_DIR" >&2
    exit 1
fi

if [ ! -f "$TMUX_CONF" ]; then
    echo "Missing tmux test config: $TMUX_CONF" >&2
    exit 1
fi

if ! command -v Xephyr >/dev/null 2>&1; then
    echo "Xephyr not found in PATH" >&2
    exit 1
fi

if ! command -v tmux >/dev/null 2>&1; then
    echo "tmux not found in PATH" >&2
    exit 1
fi

HOST_DISPLAY=${DISPLAY:-}
if [ -z "$HOST_DISPLAY" ]; then
    echo "DISPLAY is not set; Xephyr needs a host X display" >&2
    exit 1
fi

find_free_display() {
    display_num=100
    while [ -e "/tmp/.X11-unix/X${display_num}" ]; do
        display_num=$((display_num + 1))
    done
    echo ":${display_num}"
}

RUNTIME_DIR=$(mktemp -d "${TMPDIR:-/tmp}/xwmux-xephyr.XXXXXX")
export XWMUX_TMUX_SOCKET="$RUNTIME_DIR/tmux.sock"
XEPHYR_DISPLAY=$(find_free_display)

cleanup() {
    set +e
    if [ -n "${XWMUX_PID:-}" ]; then
        kill "$XWMUX_PID" 2>/dev/null || true
        wait "$XWMUX_PID" 2>/dev/null || true
    fi
    if [ -S "$XWMUX_TMUX_SOCKET" ]; then
        tmux -S "$XWMUX_TMUX_SOCKET" kill-server 2>/dev/null || true
    fi
    if [ -n "${XEPHYR_PID:-}" ]; then
        kill "$XEPHYR_PID" 2>/dev/null || true
        wait "$XEPHYR_PID" 2>/dev/null || true
    fi
    rm -rf "$RUNTIME_DIR"
}
trap cleanup EXIT INT TERM

DISPLAY="$HOST_DISPLAY" Xephyr "$XEPHYR_DISPLAY" -screen 1280x800 -ac +xinerama &
XEPHYR_PID=$!

for _ in $(seq 1 50); do
    if [ -e "/tmp/.X11-unix/X${XEPHYR_DISPLAY#:}" ]; then
        break
    fi
    sleep 0.1
done

if [ ! -e "/tmp/.X11-unix/X${XEPHYR_DISPLAY#:}" ]; then
    echo "Xephyr did not start on $XEPHYR_DISPLAY" >&2
    exit 1
fi

export DISPLAY="$XEPHYR_DISPLAY"

tmux -f "$TMUX_CONF" -S "$XWMUX_TMUX_SOCKET" new-session -d -s default
"$HOOKS_SCRIPT"

echo "Starting xwmux test instance"
echo "  HOST_DISPLAY=$HOST_DISPLAY"
echo "  DISPLAY=$DISPLAY"
echo "  XWMUX_TMUX_SOCKET=$XWMUX_TMUX_SOCKET"
echo "  TMUX_CONF=$TMUX_CONF"
if [ -n "${XWMUX_TERMINAL:-}" ]; then
    echo "  XWMUX_TERMINAL=$XWMUX_TERMINAL"
fi
if [ "$VERBOSE" -eq 1 ]; then
    echo "  XWMUX_VERBOSE=-vv"
    echo "--- xwmux logs follow ---"
    "$XWMUX_BIN" -vv &
else
    "$XWMUX_BIN" &
fi
XWMUX_PID=$!
wait "$XWMUX_PID"
