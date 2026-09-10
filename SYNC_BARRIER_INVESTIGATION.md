# Sync Barrier Investigation

## Purpose

This document explains the current tmux-to-xwmux refresh flow, why the new sync acknowledgment is not yet safe to replace every timing wait, and how to investigate and fix that gap.

It is aimed at someone unfamiliar with xwmux.

## Relevant architecture

`xwmux` is an X11 window manager that associates GUI X windows with tmux panes.

The relevant components are:

- `src/scripts/xwmux-tmux-conf.sh`
  - registers tmux hooks for pane, window, session, and client changes.
- `src/scripts/xwmux-report.sh`
  - hook target; sends `xwmux-ctl refresh`.
- `src/xwmux/tmux.cpp`
  - reads a complete tmux pane snapshot.
- `src/xwmux/instance.cpp`
  - receives X11 IPC messages and reconciles the snapshot into X11 state.
- `src/xwmux/ipc.h`
  - defines the X11 client-message protocol.
- `src/xwmux-utils/xwmux-ctl.cpp`
  - sends protocol messages to the running WM.
- `tests/xwmux_test_harness.py`
  - launches an isolated nested X server/tmux/xwmux instance for integration tests.

## Refresh model

Tmux hooks do not send pane-by-pane state. They only request refresh:

```text
tmux action
  -> tmux hook
  -> xwmux-report.sh
  -> xwmux-ctl refresh
  -> WM reads tmux snapshot
  -> WM reconciles X11 state
```

The reconciliation path in `WMInstance::refresh_from_tmux_snapshot()` is structured as:

1. read a tmux snapshot;
2. remove mappings whose panes disappeared;
3. move mapped panes to their current tmux window;
4. find the focused pane and apply current attachment/focus policy;
5. apply geometry to every mapped pane.

The focused pane is obtained from the attached tmux client's actual active window/pane. This matters because `list-panes -a` can report an active pane for more than one session.

## Current sync primitive

The IPC protocol now supports:

```text
xwmux-ctl sync <sequence>
```

When the WM receives `SYNC(sequence)`, it:

1. performs a full tmux snapshot refresh;
2. writes `sequence` to root-window property `_XW_SYNC_SEQ`.

The test harness exposes:

- `wait_until_wm_synced()`
- `tmux_and_wait(...)`

Both wait until `_XW_SYNC_SEQ` acknowledges their requested sequence.

This establishes that the WM completed *the refresh requested by sync*. It is useful for actions where the snapshot is already stable.

## The prefix-readiness problem

`tests/test_prefix.py` historically waits two seconds before sending tmux prefix sequences. The wait follows GUI launch/split/focus churn.

An attempt to replace that sleep with `wait_until_wm_synced()` made prefix tests fail. The change was reverted; the existing tests remain green.

The failure means that acknowledging one refresh is not yet sufficient proof that it is safe to send the next synthetic key sequence. It does **not** by itself prove the cause.

Two plausible causes are:

1. **Delayed hook refresh**
   - a tmux hook from an earlier action may send another `REFRESH` after `SYNC` has acknowledged;
   - if an unchanged refresh performs X11 focus or visibility side effects, it could disturb a temporary prefix override.

2. **X11/client input readiness**
   - the WM may have processed focus/grab requests, while the terminal/client has not yet reached the state needed to receive the next synthetic key event;
   - this is independent of tmux snapshot correctness.

Do not assume the first explanation without a reproducing test.

## Desired refresh invariant

A refresh from an unchanged tmux snapshot should be convergent and harmless:

- no unnecessary `XMapWindow` / `XUnmapWindow` calls;
- no unnecessary X focus changes;
- no prefix-grab changes unless the desired focus target changes;
- no changes to effective GUI visibility or geometry.

In particular, while tmux prefix is active and xwmux has temporarily focused the root terminal, a duplicate refresh must preserve that override unless tmux state itself changed.

## Investigation plan

### 1. Add a direct regression test

Create a headless test with this sequence:

1. launch and focus a GUI pane;
2. send prefix once;
3. wait until the root terminal has X focus and tmux reports prefix active;
4. explicitly run `xwmux-ctl refresh` (or request a controlled sync);
5. assert that:
   - root terminal still has X focus;
   - tmux prefix remains active;
   - the GUI pane remains visible;
   - xwmux's override/prefix state remains consistent.

Interpretation:

- if it fails, duplicate refresh handling is the concrete bug;
- if it passes, investigate X11 keyboard-grab/client readiness instead.

### 2. Instrument refresh ordering if needed

Use the existing debug logging in `src/xwmux/instance.cpp` to record:

- receipt of `REFRESH` and `SYNC` messages;
- snapshot active location;
- whether prefix override is active;
- each focus, map, and unmap operation.

A test can then show whether a later refresh arrived after sync acknowledgment and whether it changed X state.

### 3. Make presentation application diff-based

If duplicate refreshes are not harmless, keep a small applied-presentation state in `WMInstance`, such as:

- visible tmux window;
- zoomed pane, if any;
- focused X target / active tmux location;
- prefix override state.

Compute desired presentation from the snapshot, then only issue X11 operations when desired state differs from applied state.

Geometry can remain a separate pass initially. It is safe to optimize geometry updates later.

### 4. Retry the prefix sleep removal

Only after the direct regression passes should `tests/test_prefix.py` replace its sleep with:

1. `wait_until_wm_synced()`;
2. explicit assertion that expected X focus is stable;
3. synthetic prefix input.

The behavior assertion must remain; sync should replace an arbitrary readiness delay, not replace correctness checks.

## Investigation results

The direct prefix-then-sync regression was added in `tests/test_sync.py` and passes:

1. focus a GUI pane;
2. activate tmux prefix mode;
3. perform a sync-triggered duplicate refresh;
4. verify terminal focus, tmux prefix state, and the GUI mapping remain intact.

Therefore, a duplicate refresh alone does not break a stable prefix override.

A split-specific headless experiment produced a different result:

1. launch/focus a GUI pane;
2. run `tmux split-window -h -d`;
3. wait for sync acknowledgment;
4. send the first prefix and then `!` immediately.

Measured behavior:

- 0 ms and 50 ms delay before the first prefix: `!` was lost;
- approximately 200 ms or more: the command succeeded.

Verbose WM logs showed one real race: a delayed refresh from the split can arrive just after xwmux has focused the terminal for prefix handling, while tmux still reports `client_prefix=0`. That refresh clears the override and refocuses the GUI.

A speculative local retry for this `client_prefix` propagation race was tested and reverted. It fixed runs containing that delayed refresh, yet other runs still lost the *first* programmatically injected prefix before a refresh could intervene.

The final fix was to stop using `tmux send-keys -K` for the captured prefix. xwmux's prefix grab is synchronous, so it now:

1. changes X focus to the terminal (or back to the GUI for double prefix);
2. synchronizes that focus request;
3. calls `XAllowEvents(..., ReplayKeyboard, ...)`.

`ReplayKeyboard` replays the original captured X key event exactly once to its new focus target. This is preferable to injecting a second XTEST key chord: it avoids duplicate input and preserves the complete original modifier/key event. The double-prefix GUI path now also receives the proper control byte (`0x02`) instead of the old `XSendEvent` plain `b` behavior.

The split/sync/immediate-prefix regression now passes without a sleep.

## Current status

- Snapshot refresh, adjacent refresh coalescing, and the sync acknowledgment are implemented.
- The harness has sync helpers, a window-switch integration test, a prefix-override duplicate-refresh regression test, and a split/sync/immediate-prefix regression test.
- Prefix tests use `wait_until_wm_synced()` instead of their former two-second sleep.
- The full headless suite passes with 22 tests.
