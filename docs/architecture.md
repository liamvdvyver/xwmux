# Architecture

## Roles

`xwmux` is an X11 window manager which uses tmux panes as the layout model.
It selects `SubstructureRedirectMask` on the root window, so applications map
through it. It also listens for root structure/substructure notifications. A
second window manager causes startup to fail.

There are four cooperating pieces:

| Piece | Responsibility |
| --- | --- |
| `xwmux` | Owns X window management, X focus, GUI window visibility and geometry, and the tmux-pane/X-window mapping. |
| Root terminal | The terminal started by `xwmux-launch-term.sh`; its class is `xwmux_root`. It displays the tmux client and is kept full-screen behind GUI windows. |
| tmux | Authoritative source for panes, pane geometry, active pane, zoom state, and pane lifetime. |
| `xwmux-ctl` | Sends X `ClientMessage`s to the root window. The terminal init script and tmux hooks use it to report information or request reconciliation. |

The root terminal runs `xwmux-init-term.sh`. That script obtains its pixel and
character-grid dimensions through terminal XTWINOPS queries, reads tmux's
`status`, `status-position`, and `prefix` options, reports layout and prefix to
the WM, then attaches/creates its selected tmux session. See
[IPC and tmux integration](ipc-and-tmux.md).

## State and ownership

The WM maintains a `TmuxXWindowMapping`, not a second independent pane model.
It stores:

- a workspace per tmux window ID (`@…`), containing GUI X windows keyed by tmux
  pane ID (`%…`);
- inverse X-window-to-pane and pane-to-tmux-window maps;
- the most recently applied active `(tmux window, pane)` location; and
- an `overridden` flag used while tmux is consuming a prefix sequence.

A tmux pane can be an ordinary terminal pane with no GUI mapping. A mapped GUI
window is owned by its associated pane: when the application window goes away,
the WM removes the mapping and normally kills that pane; when a pane disappears
from a snapshot, the WM asks its mapped X client to close and removes the
mapping.

`WindowPane` additionally records whether xwmux hid the X window, pending
unmap requests, and whether a close has already been requested. The pending
unmap count prevents an `UnmapNotify` caused by xwmux hiding a window from
being confused with application withdrawal.

## Source of truth and reconciliation

tmux is queried as a snapshot, rather than incrementally parsed from hooks.
`TmuxClient::snapshot()` obtains the current client location using
`display-message -p '#{window_id} #{pane_id}'` and all panes with
`list-panes -a`. Each snapshot record includes its tmux location, cell-space
rectangle, zoom flag, `pane_pid`, and whether it equals that active location.

On a refresh the WM:

1. removes mappings whose pane IDs no longer occur in the snapshot;
2. moves existing mappings if a pane moved to another tmux window;
3. applies attachment/focus/visibility using the one focused snapshot pane; and
4. applies geometry to every mapped snapshot pane.

Thus a hook only signals that tmux may have changed; the snapshot determines
what changed. Consecutive queued refresh messages are coalesced before one
snapshot is read. The detailed ordering and exceptional cases are in
[Event flows](event-flows.md).

## Coordinate systems

Tmux reports pane rectangles in terminal character cells. The root terminal
reports both its character grid and pixel grid. `WindowLayouts` combines those
with the X screen size to convert cell positions to X pixel rectangles. If the
tmux status bar is at the top, pane rectangles are shifted down one cell; with
a bottom bar the usable terminal height excludes the last cell. GUI geometry is
not applied until the terminal layout report has arrived.

On root-window `ConfigureNotify`, xwmux updates the screen resolution and
restarts the root terminal. When that terminal maps again, it is resized to the
screen and lowered; a forced active-pane refocus restores GUI focus if needed.

## Further reading

- [Event flows](event-flows.md) — lifecycle and reconciliation paths.
- [Prefix handling](prefix-handling.md) — why the terminal and GUI exchange X
  focus for tmux commands.
- [IPC and tmux integration](ipc-and-tmux.md) — message boundary and hooks.
