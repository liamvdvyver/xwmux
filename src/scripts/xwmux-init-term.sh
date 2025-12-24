#!/usr/bin/env bash

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
pixel_height=${reply%%;*}    # first number
pixel_width=${reply#*;}      # second number

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

if ! tmux list-sessions; then
    tmux new-session -d -s "$session_name"
fi

status_position=$(tmux show-options -g status-position | cut -f 2 -d ' ')
prefix=$(tmux show-options -g prefix | cut -f 2 -d ' ')

xwmux-ctl init "$rows" "$cols" "$pixel_width" "$pixel_height" "$status_position"
xwmux-ctl prefix "$prefix"

tmux new-session -A -s "$session_name"
