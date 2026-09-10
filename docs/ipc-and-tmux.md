# IPC and tmux integration

## X11 IPC boundary

`xwmux-ctl` opens the current X display and sends a 32-bit `ClientMessage` to
the root window with `SubstructureRedirectMask`. This reaches the WM that owns
the root redirect. There is no Unix-domain socket in the active implementation.

| `xwmux-ctl` command | X atom | WM action |
| --- | --- | --- |
| `init rows cols px_w px_h bar-position` | `_XW_RESOUTION` | Set terminal cell/pixel layout and status-bar position. (The atom spelling is intentionally shown as implemented.) |
| `prefix key` | `_XW_PREFIX` | Store/replace the X prefix keycode and modifiers. |
| `refresh` | `_XW_REFRESH` | Re-read and reconcile the tmux snapshot. Adjacent refresh events are coalesced. |
| `sync sequence` | `_XW_SYNC` | Refresh, then write `sequence` to the root `_XW_SYNC_SEQ` property. |
| `dump-state` | `_XW_DUMP_STATE` | Serialize WM mapping state to root `_XW_STATE_JSON`. |
| `kill-pane %ID` / `focused` | `_XW_KILL_PANE` | Ask the matching mapped X client to close and remove its mapping. |
| `kill-pane orphans` | `_XW_KILL_ORPHANS` | Close mappings whose panes do not exist in tmux. |
| `exit` | `_XW_EXIT` | Stop xwmux. |

`dump-state` first removes the old property, sends its request, then polls for
the new JSON property for up to one second. It is a diagnostic snapshot of
xwmux's mapping and includes active location, workspaces, inverse maps,
`overridden`, layout initialization, root terminal ID, pending windows, and
prefix-grab state. It is not a direct tmux query.

`sync` is the explicit ordering primitive used by the test harness: observing
`_XW_SYNC_SEQ` means the WM completed a tmux snapshot refresh triggered by that
request. It does not make unrelated future tmux hooks synchronous.

## Terminal initialization

The root terminal launcher chooses a supported terminal and runs
`xwmux-init-term.sh`. The init script:

1. queries the terminal for pixel dimensions (XTWINOPS `14t`) and text-grid
   dimensions (`18t`);
2. chooses `tmux -S "$XWMUX_TMUX_SOCKET"` when that variable is nonempty,
   otherwise `tmux`;
3. creates a detached `default` session if no server is available;
4. reads global `status`, `status-position`, and `prefix` options;
5. reports the layout and prefix through `xwmux-ctl`; and
6. resolves a target session from the WM's active mapping when possible, then
   runs `tmux new-session -A -s <session>`.

The target-session step lets a restarted root terminal reconnect to the session
which contains the currently active mapped pane rather than always using
`default`.

## Current hook installation

The current user-facing setup in the top-level README asks tmux configuration
to run `xwmux-tmux-conf.sh`. That script uses hook slot `69`, enables tmux
`focus-events`, and registers `run-shell "xwmux-report.sh"` for these hooks:

- `after-split-window`, `pane-exited`, `window-pane-changed`;
- `client-session-changed`, `session-window-changed`, `after-select-window`;
- `after-new-window`, `after-new-session`, `client-attached`;
- `after-resize-pane`, `after-select-layout`, and `pane-focus-in`.

`xwmux-report.sh` currently consists of `xwmux-ctl refresh`. The
`after-kill-pane` hook first runs `xwmux-ctl kill-pane orphans` and then the
report script. This document describes the checked-in behavior only; hook
installation and script organization may be changed independently of these
architecture notes.

All scripts and the WM-side tmux client honor `XWMUX_TMUX_SOCKET`. The C++
client uses it for tmux commands such as the snapshot query, pane creation,
pane killing, pane naming, prefix-state query, and Escape delivery.

## Snapshot data used by the WM

For each refresh, `TmuxClient::snapshot()` obtains the active location from
`list-clients`, using the most recently active client. This also succeeds while
there are no attached clients during a detach/reattach transition; in that
case the snapshot has no focused pane.

It obtains every pane from `list-panes -a` with these fields:

```text
window_zoomed_flag window_id pane_id pane_left pane_top pane_width pane_height pane_pid
```

The WM uses the active location to mark one record focused, represents the
rectangle in terminal-cell coordinates, and sorts focused and empty (`pane_pid
== 0`) records first. The reconciliation semantics are documented in
[Event flows](event-flows.md).

## Environment boundary

`XWMUX_TMUX_SOCKET` must be exported before both xwmux and its helper script
path are started to select a dedicated tmux server. Without it, the default
tmux server is used. `XWMUX_TERMINAL` (or fallback `TERMINAL`) controls the
root terminal launcher; supported named launch paths are kitty, alacritty, st,
and xterm.
