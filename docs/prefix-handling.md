# Prefix handling

xwmux lets tmux prefix bindings work while a GUI X window has focus. It does
this with a root-window passive X key grab and an explicit focus handoff; it
does not inject a separate tmux key sequence.

The key and modifiers come from tmux's global `prefix` option. The root terminal
initialization script reports that option with `xwmux-ctl prefix`, which converts
the tmux key spelling to an X keycode/modifier pair. If a new prefix report
arrives while the key is already grabbed, `XState::set_prefix` replaces the old
grab with the new one.

## Normal focus state

When refresh selects a mapped GUI pane, xwmux focuses that GUI X window and
calls `XGrabKey` on the root window for the reported prefix. The grab is
synchronous for the keyboard, so xwmux receives the captured `KeyPress` before
it is delivered to the focused GUI client.

When refresh selects a terminal-only pane, xwmux focuses the root terminal and
ungrabs the prefix. In this state the terminal's tmux client receives the key
normally.

## First prefix while a GUI is active

1. The root grab delivers the matching `KeyPress` to xwmux.
2. xwmux sets the mapping's `overridden` flag and moves X input focus to the
   root terminal.
3. It synchronizes with X, then uses `XAllowEvents(..., ReplayKeyboard, ...)`.
   The originally captured key is replayed to the terminal, so tmux enters its
   prefix state.
4. The active pane mapping remains the GUI pane, but X focus is intentionally
   on the terminal while tmux waits for the next prefix key.

A refresh during this interval checks `#{client_prefix}`. If tmux still says
prefix is active, xwmux updates workspace visibility and active-pane bookkeeping
without stealing focus back from the terminal. This preserves the temporary
override even if a hook refreshes state.

## Pointer selection during a prefix override

Selecting a visible mapped GUI client while the override is active also ends
the prefix sequence. xwmux sends tmux Escape, clears the override, and selects
the client’s pane. This includes selecting the GUI client that was already
active before the prefix: pointer interaction leaves tmux and X focus in the
same clean state as a completed tmux command.

## Completing a tmux command

The next key is delivered to the terminal/tmux client. A tmux command may
select a pane, resize or zoom a layout, break a pane into another tmux window,
or otherwise trigger a hook. The subsequent snapshot refresh applies the new
active location, visibility, geometry, and focus. Once tmux reports that prefix
is no longer active, an overridden refresh clears the override and refocuses the
active pane.

This is why normal prefix bindings such as prefix-`z` can preserve focus on a
GUI pane after zooming, while selecting a terminal pane places focus on the root
terminal.

## Prefix twice: send the prefix to the GUI

If a second matching prefix `KeyPress` occurs while `overridden` is set:

1. xwmux sends tmux an Escape key (`tmux send-keys -K escape`) to cancel tmux
   prefix mode;
2. it clears the override and refocuses the active GUI pane;
3. after an X synchronization, it replays the captured second prefix key.

Because focus changed before replay, the GUI client receives that second prefix
key. The integration test verifies this with a GUI key logger.

## Preconditions and limits

- No grab is installed until the terminal init path has reported a prefix.
- xwmux compares the X event keycode and modifier state exactly to the reported
  pair.
- The behavior concerns tmux's configured `prefix` key. Non-prefix tmux
  bindings do not use this handoff; the user-facing README notes that global
  bindings such as `bind-key -n` need a separate hotkey mechanism.

Related documents: [Architecture](architecture.md), [event reconciliation](event-flows.md),
and [IPC/tmux integration](ipc-and-tmux.md).
