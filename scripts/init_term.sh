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

rows=$(tput lines)
cols=$(tput cols)

xwmux-ctl res "$rows" "$cols"
tmux new-session -A -s default
