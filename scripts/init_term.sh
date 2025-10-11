#!/bin/env sh
# Register the dimensions (rows/columns) of the current terminal for layout calculation.
#
# The socket should be passed in as $1, or inferred from $xwmux.
#
# NOTE: $xwmux is not usually set if the shell is a child of a process launched before xwmux.
# This includes:
# * Running it from tmux (child of server)
# * Running it from a hotkey daemon

xwmux_sock_default="/tmp/xwmux.sock"
if [ -n "$1" ]; then
    xwmux_sock="$1"
fi

if [ -z "$xwmux_sock" ]; then
    xwmux_sock="$xwmux_sock_default"
fi

session_name="default"

rows=$(tput lines)
cols=$(tput cols)

if ! tmux list-sessions; then
    tmux new-session -d -s "$session_name"
fi

status_position=$(tmux show-options -g status-position | cut -f 2 -d ' ')

xwmux-ctl init "$rows" "$cols" "$status_position"

tmux new-session -A -s "$session_name"
