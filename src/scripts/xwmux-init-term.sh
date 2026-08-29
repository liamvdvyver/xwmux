#!/usr/bin/env bash

# Register the dimensions (rows/columns) of the current terminal for layout calculation.

session_name="default"

if [ -n "$XWMUX_TMUX_SOCKET" ]; then
    tmux_cmd() {
        tmux -S "$XWMUX_TMUX_SOCKET" "$@"
    }
else
    tmux_cmd() {
        tmux "$@"
    }
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

status=$(tmux_cmd show-options -g status | cut -f 2 -d ' ')
if [ "$status" = "off" ]; then
    bar_position=off
else
    bar_position=$(tmux_cmd show-options -g status-position | cut -f 2 -d ' ')
fi
prefix=$(tmux_cmd show-options -g prefix | cut -f 2 -d ' ')

xwmux-ctl init "$rows" "$cols" "$pixel_width" "$pixel_height" "$bar_position"
xwmux-ctl prefix "$prefix"

tmux_cmd new-session -A -s "$session_name"
