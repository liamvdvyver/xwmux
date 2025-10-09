#!/bin/env sh

if [ -z "$1" ]; then
    exit 1
fi

msg=$(tmux display-message -p '#{session_id}#{window_id}#{pane_id}')
eval xwmux-ctl tmux-event "$1" \'"$msg"\' 2>/dev/null
