#!/usr/bin/env sh

if [ -n "$XWMUX_TMUX_SOCKET" ]; then
    tmux_cmd() {
        tmux -S "$XWMUX_TMUX_SOCKET" "$@"
    }
else
    tmux_cmd() {
        tmux "$@"
    }
fi

# Update focus
msg=$(tmux_cmd display-message -p '#{q:session_id} #{window_id} #{pane_id}')
eval xwmux-ctl tmux-focus "$msg" 2>/dev/null

panes=$(tmux_cmd list-panes -F "#{pane_active} #{window_zoomed_flag} #{q:session_id} #{window_id} #{pane_id} #{pane_left} #{pane_top} #{pane_width} #{pane_height} #{pane_dead}")

# Update layout
echo "$panes" |
    sort -r |
    xargs -d '\n' -I {} sh -c "xwmux-ctl tmux-position {} || exit 0"
