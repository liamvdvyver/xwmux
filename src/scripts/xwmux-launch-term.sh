#!/usr/bin/env bash

ROOT_TERM_CLASS="xwmux_root"
EXEC_PROGRAM="xwmux-init-term.sh"
TMUX_SOCKET=""

usage() {
    echo "Usage: $0 [--tmux-socket <path>]" >&2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --tmux-socket)
            if [ -n "$TMUX_SOCKET" ] || [ "$#" -lt 2 ]; then
                usage
                exit 1
            fi
            TMUX_SOCKET="$2"
            shift 2
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done

tmux_cmd() {
    if [ -n "$TMUX_SOCKET" ]; then
        tmux -S "$TMUX_SOCKET" "$@"
    else
        tmux "$@"
    fi
}

# Start the server before reading its global configuration. This lets a fresh
# xwmux session use options from the tmux config that initialized its server.
tmux_cmd start-server
configured_terminal=$(tmux_cmd show-options -gv @xwmux-terminal 2>/dev/null || true)

SELECTED=""
if [ -n "$configured_terminal" ]; then
    SELECTED="$configured_terminal"
elif [ -n "${TERMINAL:-}" ]; then
    case "$TERMINAL" in
        kitty|alacritty|st|xterm)
            SELECTED="$TERMINAL"
            ;;
    esac
fi

if [ -z "$SELECTED" ]; then
    if command -v kitty >/dev/null 2>&1; then
        SELECTED="kitty"
    elif command -v alacritty >/dev/null 2>&1; then
        SELECTED="alacritty"
    elif command -v st >/dev/null 2>&1; then
        SELECTED="st"
    elif command -v xterm >/dev/null 2>&1; then
        SELECTED="xterm"
    else
        echo "xwmux: no supported terminal found" >&2
        exit 1
    fi
fi

case "$SELECTED" in
    kitty|alacritty|st|xterm)
        ;;
    *)
        echo "xwmux: unsupported @xwmux-terminal value: $SELECTED" >&2
        exit 1
        ;;
esac

if ! command -v "$SELECTED" >/dev/null 2>&1; then
    echo "xwmux: configured terminal is unavailable: $SELECTED" >&2
    exit 1
fi

INIT_ARGS=()
if [ -n "$TMUX_SOCKET" ]; then
    INIT_ARGS=(--tmux-socket "$TMUX_SOCKET")
fi

case "$SELECTED" in
    kitty)
        kitty --class "$ROOT_TERM_CLASS" --exec "$EXEC_PROGRAM" "${INIT_ARGS[@]}" &
        ;;
    alacritty)
        alacritty --class "$ROOT_TERM_CLASS" --command "$EXEC_PROGRAM" "${INIT_ARGS[@]}" &
        ;;
    st)
        st -c "$ROOT_TERM_CLASS" -e "$EXEC_PROGRAM" "${INIT_ARGS[@]}" &
        ;;
    xterm)
        xterm -class "$ROOT_TERM_CLASS" -b 0 -e "$EXEC_PROGRAM" "${INIT_ARGS[@]}" &
        ;;
esac
