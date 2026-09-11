#!/usr/bin/env bash

# Start a completely private X and tmux environment for integration tests.
# Do not use the caller's DISPLAY: doing so would make Xephyr visible on the
# developer's desktop.
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
TMUX_CONF="$SCRIPTS_DIR/xwmux-test.tmux.conf"

export PATH="$BUILD_DIR:$SCRIPTS_DIR:$PATH"
# In particular, do not let a nested command discover the developer's tmux
# client through its environment.  Every test tmux command uses the socket
# below.
unset DISPLAY XAUTHORITY TMUX XWMUX_TMUX_SOCKET XWMUX_TERMINAL TERMINAL

if [ ! -x "$XWMUX_BIN" ] || [ ! -x "$CTL_BIN" ]; then
    echo "Missing built binaries in $BUILD_DIR" >&2
    echo "Run: cmake -S $ROOT_DIR -B $BUILD_DIR && cmake --build $BUILD_DIR" >&2
    exit 1
fi

if [ ! -f "$TMUX_CONF" ]; then
    echo "Missing tmux test config: $TMUX_CONF" >&2
    exit 1
fi

for program in Xvfb Xephyr tmux; do
    if ! command -v "$program" >/dev/null 2>&1; then
        echo "$program not found in PATH" >&2
        exit 1
    fi
done

find_free_display() {
    display_num=100
    while [ -e "/tmp/.X11-unix/X${display_num}" ]; do
        display_num=$((display_num + 1))
    done
    echo ":${display_num}"
}

# All cleanup targets below are either children started by this script or a
# tmux server addressed through this freshly-created, private socket path.
RUNTIME_DIR=$(mktemp -d "${TMPDIR:-/tmp}/xwmux-xephyr.XXXXXX")
chmod 700 "$RUNTIME_DIR"
TMUX_SOCKET="$RUNTIME_DIR/tmux.sock"
XWMUX_PID=""
TMUX_PID=""
XEPHYR_PID=""
XVFB_PID=""
CLEANUP_DONE=0

terminate_child() {
    pid=$1
    [ -n "$pid" ] || return 0
    kill -TERM "$pid" 2>/dev/null || true
    for _ in $(seq 1 50); do
        # A direct child may already be a zombie while its shell has not yet
        # reaped it. kill -0 succeeds for zombies, so reap instead of spending
        # the whole timeout and being killed before remaining cleanup runs.
        if ! kill -0 "$pid" 2>/dev/null || ps -o stat= -p "$pid" 2>/dev/null | grep -q 'Z'; then
            wait "$pid" 2>/dev/null || true
            return 0
        fi
        sleep 0.1
    done
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

cleanup() {
    [ "$CLEANUP_DONE" -eq 0 ] || return 0
    CLEANUP_DONE=1
    set +e
    terminate_child "$XWMUX_PID"
    # This is deliberately socket-addressed, never a server-name or process
    # name lookup, so an interactive tmux server cannot be selected.
    if [ -S "$TMUX_SOCKET" ]; then
        tmux -S "$TMUX_SOCKET" kill-server 2>/dev/null || true
    fi
    # tmux daemonizes into its own session, so it is not necessarily in the
    # launcher's process group.  This PID was queried from our private socket.
    terminate_child "$TMUX_PID"
    terminate_child "$XEPHYR_PID"
    terminate_child "$XVFB_PID"
    rm -rf "$RUNTIME_DIR"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

# Always create a private headless parent display, even if the caller has a
# DISPLAY.  Xephyr is therefore never mapped on the caller's desktop.
HOST_DISPLAY=$(find_free_display)
Xvfb "$HOST_DISPLAY" -screen 0 1280x800x24 -ac -nolisten tcp &
XVFB_PID=$!

for _ in $(seq 1 50); do
    if [ -e "/tmp/.X11-unix/X${HOST_DISPLAY#:}" ]; then
        break
    fi
    if ! kill -0 "$XVFB_PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if [ ! -e "/tmp/.X11-unix/X${HOST_DISPLAY#:}" ]; then
    echo "Xvfb did not start on $HOST_DISPLAY" >&2
    exit 1
fi

XEPHYR_DISPLAY=$(find_free_display)
DISPLAY="$HOST_DISPLAY" Xephyr "$XEPHYR_DISPLAY" -screen 1280x800 -ac +xinerama -nolisten tcp &
XEPHYR_PID=$!

for _ in $(seq 1 50); do
    if [ -e "/tmp/.X11-unix/X${XEPHYR_DISPLAY#:}" ]; then
        break
    fi
    if ! kill -0 "$XEPHYR_PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if [ ! -e "/tmp/.X11-unix/X${XEPHYR_DISPLAY#:}" ]; then
    echo "Xephyr did not start on $XEPHYR_DISPLAY" >&2
    exit 1
fi

export DISPLAY="$XEPHYR_DISPLAY"
tmux -f "$TMUX_CONF" -S "$TMUX_SOCKET" new-session -d -s default
TMUX_PID=$(tmux -S "$TMUX_SOCKET" display-message -p '#{pid}')

echo "Starting xwmux test instance"
echo "  HOST_DISPLAY=$HOST_DISPLAY"
echo "  DISPLAY=$DISPLAY"
echo "  TMUX_SOCKET=$TMUX_SOCKET"
echo "  TMUX_CONF=$TMUX_CONF"
echo "  STARTED_XVFB=1"
if [ "$VERBOSE" -eq 1 ]; then
    echo "--- xwmux logs follow ---"
    "$XWMUX_BIN" --tmux-socket "$TMUX_SOCKET" -vv &
else
    "$XWMUX_BIN" --tmux-socket "$TMUX_SOCKET" &
fi
XWMUX_PID=$!
wait "$XWMUX_PID"
