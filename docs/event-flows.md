# Event flows

This page follows the code paths in `WMInstance` and `TmuxClient`. tmux hooks
and `xwmux-ctl refresh` are notifications, not a pane-by-pane protocol; each
refresh reads a new tmux snapshot.

## Startup and layout initialization

1. `WMInstance::run()` opens the root terminal through
   `xwmux-launch-term.sh`.
2. Its `MapRequest` is recognized by the `xwmux_root` class. xwmux records it,
   sizes it to the X screen, lowers it, and maps it. If no mapped GUI pane is
   active, it focuses the terminal.
3. `xwmux-init-term.sh`, running in that terminal, queries terminal dimensions
   and tmux status/prefix options. It sends `xwmux-ctl init …` and
   `xwmux-ctl prefix …` before attaching to tmux.
4. `RESOLUTION` initializes the cell-to-pixel layout conversion. `PREFIX`
   records the X keycode/modifier pair. Until layout initialization, an attached
   GUI window can be mapped but its initial and subsequent pane geometry is
   deferred.

A root-size change closes the current root terminal. Its `DestroyNotify` clears
the terminal reference and opens a replacement; its next map performs the
normal root-terminal path above.

## Creating and attaching a GUI window

1. An application asks X to map a non-root, non-`override_redirect` window.
   xwmux receives `MapRequest`.
2. It adds the X window to a FIFO pending queue and set, asks tmux to run
   `split-window '' \; break-pane`, and selects `PropertyChangeMask` on the X
   window to observe later `WM_NAME` changes. A duplicate map for a window
   already pending is ignored.
3. A later tmux refresh reads the full snapshot. For the focused pane only, the
   WM attaches the queue head when all of these are true: there is a pending
   window, that pane has no GUI mapping, and its `pane_pid` is zero. This is
   how the newly created empty tmux pane is distinguished from an existing
   running terminal pane.
4. The attachment adds both directions of the mapping, removes the window from
   the pending set, sets its initial geometry if layout is ready, and sets the
   tmux pane title from the X `WM_NAME`.
5. The active workspace is made visible and the attached GUI X window receives
   X input focus. Its tmux pane becomes the WM's active location.

If the queued X window disappeared or was already mapped before attachment,
xwmux kills the candidate tmux pane instead. If `WM_NAME` later changes for a
mapped window, xwmux updates the corresponding tmux pane title.

## Refresh: pane movement, visibility, focus, and geometry

A `REFRESH` event is special-cased in the main event loop. Adjacent refreshes
already queued in Xlib are consumed, then one snapshot is applied in this
order:

1. **Prune.** Pane IDs absent from the snapshot are orphaned. Their X client is
   sent `WM_DELETE_WINDOW` and the mapping is removed without trying to kill an
   already-gone pane.
2. **Move.** Every snapshot record calls `move_pane`; an existing mapping follows
   the same pane ID if tmux moved it to a different tmux window (for example,
   after a break-out operation).
3. **Focused pane.** Only the snapshot record matching tmux's current client
   location controls attachment, active workspace, and X focus. Moving to a
   different tmux window hides all GUI windows in the old workspace. In the
   active workspace, a zoomed pane hides every other mapped GUI window; an
   unzoomed pane shows all of them. A GUI active pane is focused and enables the
   prefix grab; a terminal active pane focuses the root terminal and disables
   that grab.
4. **Geometry.** Every mapped snapshot pane is translated from tmux cells to
   screen pixels and resized. Unmapped panes and pre-layout state are skipped.

A zoomed active GUI pane deliberately permits a redundant X refocus even when
its tmux location did not change. This handles changes such as zoom/unzoom
where visibility and geometry change but the selected pane remains the same.

## Closing and withdrawal

- If a mapped client emits `UnmapNotify` that does **not** match a pending
  xwmux hide operation, xwmux removes its mapping, kills its tmux pane, and
  focuses the terminal. A pending hide only decrements the bookkeeping count.
- A mapped client's `DestroyNotify` similarly removes the mapping, kills its
  pane, and focuses the terminal. A pending but unattached client is simply
  removed from the pending set.
- `xwmux-ctl kill-pane %ID` or `focused` sends `WM_DELETE_WINDOW` to the mapped
  X client, removes its mapping, then expects normal unmap handling to finish
  pane cleanup. `kill-pane orphans` closes mappings for panes tmux no longer
  has.

## Prefix transition

The prefix path is intentionally separate because it must temporarily focus the
terminal while a GUI is active. See the step-by-step flow in
[Prefix handling](prefix-handling.md).

## Observable behavior

The integration tests exercise these flows: window mapping and state dumps,
focus across panes and tmux windows, zoom visibility/geometry, session changes,
closing behavior, and prefix operations. See [Testing and inspection](testing.md)
for commands and [IPC and tmux integration](ipc-and-tmux.md) for the events
which cause refreshes.
