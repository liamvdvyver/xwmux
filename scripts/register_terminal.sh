#!/bin/env sh
# Register the dimensions (rows/columns) of the current terminal for layout calculation.
# should be run before connecting to tmux session.

if [ -z "$xwmux_sock" ]; then
    echo "\$xwmux_sock not defined: Is xwmux running?" > /dev/stderr
    exit 1;
fi

rows=$(tput lines)
cols=$(tput cols)

echo "dims $rows,$cols" > "$xwmux_sock"
