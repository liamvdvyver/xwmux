#!/usr/bin/env bash

# Register the dimensions (rows/columns) of the current terminal for layout calculation.

session_name="default"
HOOK_NO=69

resolve_target_session() {
    state_json=$(xwmux-ctl dump-state 2>/dev/null || true)
    if [ -z "$state_json" ]; then
        return 0
    fi

    active_loc=$(STATE_JSON="$state_json" python3 -c '
import json
import os
import sys

try:
    state = json.loads(os.environ["STATE_JSON"])
except Exception:
    raise SystemExit(0)

active = state.get("active") or {}
window = active.get("window")
pane = active.get("pane")
if window is None or pane is None:
    raise SystemExit(0)

sys.stdout.write(f"@{window} %{pane}")
')

    if [ -z "$active_loc" ]; then
        return 0
    fi

    active_session=$(tmux_cmd list-panes -a -F '#{session_name} #{window_id} #{pane_id}' |
        while read -r candidate_session candidate_window candidate_pane; do
            if [ "$candidate_window $candidate_pane" = "$active_loc" ]; then
                printf '%s\n' "$candidate_session"
                break
            fi
        done)

    if [ -n "$active_session" ]; then
        session_name="$active_session"
    fi
}

if [ -n "$XWMUX_TMUX_SOCKET" ]; then
    tmux_cmd() {
        tmux -S "$XWMUX_TMUX_SOCKET" "$@"
    }
else
    tmux_cmd() {
        tmux "$@"
    }
fi

configure_tmux_hooks() {
    tmux_cmd set-hook -g after-split-window[$HOOK_NO]     'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g pane-exited[$HOOK_NO]            'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g window-pane-changed[$HOOK_NO]    'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g client-session-changed[$HOOK_NO] 'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g session-window-changed[$HOOK_NO] 'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g after-select-window[$HOOK_NO]    'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g after-new-window[$HOOK_NO]       'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g after-new-session[$HOOK_NO]      'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g client-attached[$HOOK_NO]        'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g after-resize-pane[$HOOK_NO]      'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g after-select-layout[$HOOK_NO]    'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g pane-focus-in[$HOOK_NO]          'run-shell "xwmux-ctl refresh"'
    tmux_cmd set-hook -g after-kill-pane[$HOOK_NO]        'run-shell "xwmux-ctl kill-pane orphans && xwmux-ctl refresh"'
    tmux_cmd set -g focus-events on
}

if [ "${1:-}" = "--configure-hooks" ]; then
    configure_tmux_hooks
    exit 0
fi

# BEGIN VIBECODED SECTION

# Query terminal for pixel size and text grid size
# Output: PIXEL_WIDTH PIXEL_HEIGHT ROWS COLS

# Save terminal settings
old=$(stty -g)

# Set raw mode (no echo, no buffering)
stty raw -echo

# Function to read a response ending with 't'
read_xtwinops() {
    local response=""
    local char
    while IFS= read -r -n1 char; do
        response+="$char"
        [[ "$char" == "t" ]] && break
    done </dev/tty
    echo "$response"
}

# Send pixel size request (XTWINOPS 14t)
printf '\033[14t' >/dev/tty
reply=$(read_xtwinops)
# reply format: ESC [ 4 ; <height> ; <width> t
reply="${reply#*$'\033['4;}" # remove ESC [ 4 ;
reply="${reply%t}"           # remove trailing t
pixel_height=${reply%%;*}     # first number
pixel_width=${reply#*;}       # second number

# Send text grid size request (XTWINOPS 18t)
printf '\033[18t' >/dev/tty
reply=$(read_xtwinops)
# reply format: ESC [ 8 ; <rows> ; <cols> t
reply="${reply#*$'\033['8;}"
reply="${reply%t}"
rows=${reply%%;*}
cols=${reply#*;}

# Restore terminal settings
stty "$old"
# END VIBECODED SECTION

if ! tmux_cmd list-sessions >/dev/null 2>&1; then
    tmux_cmd new-session -d -s "$session_name"
fi

# These hooks belong to the tmux server, so install them for every root
# terminal startup. Indexed hooks make repeated initialization idempotent.
configure_tmux_hooks

status=$(tmux_cmd show-options -g status | cut -f 2 -d ' ')
if [ "$status" = "off" ]; then
    bar_position=off
else
    bar_position=$(tmux_cmd show-options -g status-position | cut -f 2 -d ' ')
fi
prefix=$(tmux_cmd show-options -g prefix | cut -f 2 -d ' ')

xwmux-ctl init "$rows" "$cols" "$pixel_width" "$pixel_height" "$bar_position"
xwmux-ctl prefix "$prefix"
resolve_target_session

tmux_cmd new-session -A -s "$session_name"
