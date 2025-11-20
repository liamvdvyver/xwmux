#!/bin/env sh

# Update focus
msg=$(tmux display-message -p '#{q:session_id} #{window_id} #{pane_id}')
eval xwmux-ctl tmux-focus "$msg" 2>/dev/null

panes=$(tmux list-panes -F "#{pane_active} #{window_zoomed_flag} #{q:session_id} #{window_id} #{pane_id} #{pane_left} #{pane_top} #{pane_width} #{pane_height}")

# Update layout
echo "$panes" |
    sort -r |
    xargs -d '\n' -I {} sh -c "xwmux-ctl tmux-position {} || exit 0"
