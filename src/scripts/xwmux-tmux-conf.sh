#!/usr/bin/env sh
# TODO: make idempotent

HOOK_NO=69

if [ -n "$XWMUX_TMUX_SOCKET" ]; then
    tmux_cmd() {
        tmux -S "$XWMUX_TMUX_SOCKET" "$@"
    }
else
    tmux_cmd() {
        tmux "$@"
    }
fi

tmux_cmd set-hook -g after-split-window[$HOOK_NO]     'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g pane-exited[$HOOK_NO]            'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g window-pane-changed[$HOOK_NO]    'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g client-session-changed[$HOOK_NO] 'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g session-window-changed[$HOOK_NO] 'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g after-select-window[$HOOK_NO]    'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g after-new-window[$HOOK_NO]       'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g after-new-session[$HOOK_NO]      'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g client-attached[$HOOK_NO]        'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g client-session-changed[$HOOK_NO] 'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g after-resize-pane[$HOOK_NO]      'run-shell "xwmux-report.sh"'
tmux_cmd set-hook -g after-select-layout[$HOOK_NO]    'run-shell "xwmux-report.sh"'

tmux_cmd set -g focus-events on
tmux_cmd set-hook -g pane-focus-in[$HOOK_NO] 'run-shell "xwmux-report.sh"'

tmux_cmd set-hook -g after-kill-pane[$HOOK_NO] 'run-shell "xwmux-ctl kill-pane orphans && xwmux-report.sh"'
