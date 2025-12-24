#!/usr/bin/env sh

ROOT_TERM_CLASS="xwmux_root"
EXEC_PROGRAM="xwmux-init-term.sh"

SELECTED="$XWMUX_TERMINAL" || "$TERMINAL"

if [ ! "$SELECTED" ]; then
    if [ "$(command -v kitty)" ]; then
        SELECTED="kitty"
    elif [ "$(command -v alacritty)" ]; then
        SELECTED="alacritty"
    elif [ "$(command -v st)" ]; then
        SELECTED="st"
    elif [ "$(command -v xterm)" ]; then
        SELECTED="xterm"
    else
        exit 1
    fi
fi

case $SELECTED in
"kitty")
    kitty --class $ROOT_TERM_CLASS --exec $EXEC_PROGRAM &
    ;;
"alacritty")
    alacritty --class $ROOT_TERM_CLASS --command $EXEC_PROGRAM &
    ;;
"st")
    # These handle keypresses fine, but have some weirdness with padding
    st -c $ROOT_TERM_CLASS -e $EXEC_PROGRAM &
    ;;
"xterm")
    # xterm has weird padding and doesn't work with tmux in my setup
    xterm -class $ROOT_TERM_CLASS -e $EXEC_PROGRAM -b 0 &
    ;;
*)
    eval "$SELECTED"
    ;;
esac
