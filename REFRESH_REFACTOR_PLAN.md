# Refresh / State-Model Refactor Plan

This document captures the intended refactor from the current tmux-hook push model to a pull/snapshot refresh model, plus a suggested PR-by-PR breakdown. It is written so a fresh agent can pick up the work.

## Goal

Simplify code structure and event flow by replacing the current incremental tmux state push path with a refresh-driven snapshot reconciliation model.

Current model:
- tmux hooks run `xwmux-report.sh`
- script queries tmux state in shell
- script sends one or more IPC messages (`tmux-position`, etc.)
- WM incrementally updates state from partial messages

Target model:
- tmux hooks only notify `refresh needed`
- WM coalesces adjacent refresh requests
- WM pulls a complete tmux snapshot in C++
- WM reconciles all tmux-derived state in one pass

Constraints for the first implementation:
- keep a single-threaded WM
- do not add sync/sequence numbers yet
- only coalesce adjacent refresh requests for now
- preserve current behavior as much as practical

---

## Why this refactor

The current complexity comes mainly from event flow and partial incremental updates, not from X11 itself.

Main issues in the current code:
- `WMInstance::handle_client_msg<MsgType::TMUX_POSITION>` has too many responsibilities
- `TmuxXWindowMapping` mixes data model and side effects
- hook flow is redundant: one user action can trigger several hooks
- state updates are order-sensitive and partially duplicated
- tests need polling/sleeps because there is no deterministic "WM caught up" concept yet

The refactor should produce these properties:
- one refresh is sufficient to derive correct WM state from current tmux state
- repeated refreshes are harmless (idempotent / convergent)
- duplicate hooks do not require counting
- hook scripts stop encoding detailed WM protocol/state

---

## Important design decisions from discussion

### 1. Do refresh first, sync later
Do **not** design the sync protocol first.

Reason:
- sync semantics depend on what "processed" means
- that only becomes clear once refresh is snapshot-based and convergent

Desired order:
1. implement new refresh/state model
2. make it correct and convergent
3. then add sequence numbers / sync to the protocol

### 2. Single-threaded only for now
Do **not** introduce a worker thread initially.

Reason:
- X11 event loop is currently single-threaded
- threading would add substantial complexity around Xlib access and state handoff
- adjacent request coalescing can be implemented in one thread

### 3. Only coalesce adjacent refresh requests initially
Do not attempt full deferred scheduling or perfect burst coalescing yet.

Planned semantics:
- when a `REFRESH` event is received, absorb immediately adjacent queued `REFRESH` events
- if the next queued event is different, or the queue is empty, run one refresh
- do not try to coalesce across refresh execution yet

This means duplicate refreshes that arrive while a refresh is being processed may still cause another refresh later. That is acceptable for the first pass.

### 4. Hooks should become refresh triggers, not state transport
`xwmux-report.sh` should stop computing and pushing pane-by-pane updates. It should just notify the WM that tmux-derived state may have changed.

### 5. Refresh must be complete and idempotent
This is the central correctness requirement.

One refresh should be enough to derive:
- active tmux location
- pane/workspace membership
- zoom visibility
- geometry
- any pane moves between tmux windows
- stale pane removal / cleanup decisions
- pending GUI attachment decisions

Repeated refreshes from unchanged tmux state must be harmless.

---

## Intended target architecture (first phase)

This is not a required immediate file split, but the code should move in this direction.

- `WMInstance`
  - own the event loop and top-level dispatch
  - own refresh coalescing
  - own one reconciliation entry point

- `TmuxClient`
  - keep tmux command helpers
  - gain APIs for reading full tmux snapshot in C++

- `TmuxSnapshot` / related structs
  - represent the complete tmux state relevant to the WM

- `TmuxXWindowMapping`
  - preferably move toward a data/query role
  - do not add more side-effectful focus/visibility logic here

- X-side event handling
  - still handles `MapRequest`, `DestroyNotify`, `UnmapNotify`, etc.
  - should queue pending GUI windows and let refresh attach them

---

## Transitional scope recommendation

To keep the refactor manageable, do **not** try to redesign everything at once.

Recommended first-phase scope:
- replace `TMUX_POSITION` push flow with `REFRESH`
- move active pane/window/zoom/geometry updates into snapshot refresh
- keep existing terminal geometry init flow (`init`) for now
- keep existing prefix init flow for now unless it is straightforward to refresh it too

Possible later follow-up:
- move prefix/bar state into refresh as well
- simplify `TmuxXWindowMapping` further
- add sync/sequence numbers

---

## Proposed tmux snapshot content

Exact structure can vary, but the snapshot should contain enough to compute all tmux-derived WM state.

Suggested conceptual fields:

### `TmuxPaneSnapshot`
- `TmuxWindowID window_id`
- `TmuxPaneID pane_id`
- `bool focused`
- `bool zoomed`
- `bool dead`
- pane geometry / `WindowPosition` in tmux character coordinates

### `TmuxSnapshot`
- collection of `TmuxPaneSnapshot`
- active location (`TmuxLocation`) or enough info to derive it
- possibly status bar position / prefix later if migrated into refresh

Initial implementation can gather this using a small number of tmux CLI queries from C++.

---

## Reconciliation model

Create one refresh entry point, conceptually something like:
- `refresh_from_tmux_snapshot()`
- or `reconcile_tmux_snapshot(const TmuxSnapshot&)`

Recommended internal phases:

1. **Read tmux snapshot**
   - query tmux from C++
   - parse to structured state

2. **Update pane/workspace membership**
   - move panes across tmux windows if needed
   - recognize panes that exist vs no longer exist

3. **Remove stale mappings**
   - handle panes/windows that disappeared from tmux snapshot
   - preserve existing lifecycle policy where appropriate

4. **Attach pending GUI windows**
   - identify panes with no bound X window
   - assign queued pending windows deterministically
   - define deterministic ordering for both pending windows and attachable panes

5. **Compute visibility**
   - active workspace visible
   - inactive workspaces hidden
   - if zoomed, only active pane visible in active workspace

6. **Compute desired focus target**
   - GUI window if active pane has one
   - root terminal otherwise
   - preserve prefix/override behavior if still present

7. **Apply geometry**
   - convert tmux pane positions to screen positions
   - move/resize visible GUI windows

8. **Finalize state**
   - update active location
   - maintain state needed by dump/debug paths

The exact split can differ, but the key is: refresh should be a small fixed pipeline, not one large intertwined branchy handler.

---

## Adjacent refresh coalescing semantics

Keep this simple for the first pass.

Desired behavior:
- read one X event
- if it is not `REFRESH`, handle normally
- if it is `REFRESH`, absorb any immediately adjacent queued `REFRESH` events
- stop when queue is empty or next event is not `REFRESH`
- run one full refresh
- continue

Conservative boundary rule:
- do **not** coalesce across different event types yet
- if a different event is queued next, flush refresh first and then return to normal event handling

This preserves event ordering better and reduces risk.

---

## Current files involved

Core WM / state files:
- `src/xwmux/instance.h`
- `src/xwmux/instance.cpp`
- `src/xwmux/tmux.h`
- `src/xwmux/tmux.cpp`
- `src/xwmux/ipc.h`
- `src/xwmux/xwrapper.h`
- `src/xwmux/layout.h`

IPC / CLI:
- `src/xwmux-utils/xwmux-ctl.cpp`

Scripts:
- `src/scripts/xwmux-report.sh`
- `src/scripts/xwmux-tmux-conf.sh`
- `src/scripts/xwmux-init-term.sh`
- `src/scripts/xwmux-launch-term.sh`

Tests most relevant to this refactor:
- `tests/test_focus.py`
- `tests/test_prefix.py`
- `tests/test_session_switching.py`
- `tests/test_closing.py`
- `tests/test_dump_state.py`
- `tests/test_resolution_change.py`

---

## PR-by-PR breakdown

The breakdown below is intentionally incremental. Each PR should keep the tree buildable and should run targeted integration tests.

---

# PR 1: Add tmux snapshot reading in C++

## Goal
Create a C++ snapshot API in `TmuxClient` without changing the event flow yet.

## Main changes
- Add snapshot structs to `src/xwmux/tmux.h` (or a new nearby file if preferred)
- Implement `TmuxClient::snapshot()` in `src/xwmux/tmux.cpp`
- Parse tmux pane/window state in C++ instead of shell

## Suggested details
- Use as few tmux CLI queries as practical
- Keep the C++ API centered on a single snapshot-returning function
- Snapshot should include pane geometry, active/focused pane information, zoom, dead, pane/window IDs

## Non-goals
- no IPC changes yet
- no hook changes yet
- no WM refresh changes yet

## Validation
- build passes
- add small unit-ish or debug scaffolding if useful
- manually inspect snapshot formatting/logging if needed

---

# PR 2: Add `REFRESH` IPC and `xwmux-ctl refresh`

## Goal
Introduce the new protocol primitive without switching the hooks yet.

## Main changes
- In `src/xwmux/ipc.h`:
  - add `MsgType::REFRESH`
  - add `Msg::refresh(...)`
- In `src/xwmux-utils/xwmux-ctl.cpp`:
  - add a `refresh` internal command that sends the message
- In `WMInstance`:
  - add event classification/helper for refresh messages
  - it can temporarily log only, or call a stub refresh path

## Suggested details
- Keep existing `TMUX_POSITION` protocol alive for now
- `REFRESH` message needs no payload initially

## Non-goals
- do not remove old protocol yet
- do not switch scripts to use `refresh` yet

## Validation
- build passes
- `xwmux-ctl refresh` can send the message successfully in a running session

---

# PR 3: Add adjacent refresh coalescing in the event loop

## Goal
Teach the WM to batch adjacent refresh requests in one thread.

## Main changes
- In `src/xwmux/instance.cpp` / `instance.h`:
  - add helpers like:
    - `is_refresh_event(...)`
    - `consume_adjacent_refresh_events()`
    - `process_refresh_burst()`
- When a refresh event is encountered:
  - absorb adjacent queued refresh events
  - call one refresh entry point

## Suggested details
- Use X queue inspection/peek behavior to absorb only adjacent refresh events
- Stop batching at the first non-refresh event or queue-empty condition
- Keep refresh execution synchronous and single-threaded

## Non-goals
- no worker thread
- no full deferred scheduler
- no sync/sequence numbers

## Validation
- build passes
- add debug logging showing refresh burst coalescing

---

# PR 4: Implement one refresh reconciliation path in `WMInstance`

## Goal
Create the new one-pass snapshot-based tmux state update path, but keep old protocol available during transition.

## Main changes
- Add `refresh_from_tmux_snapshot()` (or equivalent) to `WMInstance`
- Use `TmuxClient::snapshot()` as the sole tmux-state input for refresh
- Move active pane/workspace/zoom/geometry logic into this refresh path

## Suggested details
- Start by handling:
  - active tmux location
  - visibility of workspaces/windows
  - zoom show/hide behavior
  - geometry updates for mapped panes
- Keep the refresh logic structured in phases
- Prefer to centralize side effects in this path rather than scattered handlers

## Transitional note
- old `TMUX_POSITION` handler may still exist while refresh path is built up
- avoid code duplication where possible, but correctness is more important than immediate cleanup

## Validation
Run at least:
- `python3 tests/test_focus.py`
- `python3 tests/test_dump_state.py`
- plus any focused manual/headless checks needed

---

# PR 5: Move pending GUI attachment into refresh

## Goal
Stop relying on pushed pane-position messages to attach new GUI windows to tmux panes.

## Main changes
- On `MapRequest` for a normal GUI client:
  - keep queueing the window as pending
  - keep creating/splitting tmux pane if that is still the policy
  - do **not** attach immediately from partial tmux push state
- In refresh reconciliation:
  - identify attachable panes with no bound X window
  - attach queued pending windows deterministically

## Suggested details
- Preserve/clarify the pending-window invariants
- Make attachment ordering deterministic
- Ensure destroyed/unmapped pending windows are cleaned up safely

## Validation
Run at least:
- `python3 tests/test_focus.py`
- `python3 tests/test_closing.py`
- `python3 tests/test_session_switching.py`

---

# PR 6: Switch hooks/scripts to refresh-only notifications

## Goal
Replace shell-side pane reporting with a single refresh trigger.

## Main changes
- Simplify `src/scripts/xwmux-report.sh` to just invoke `xwmux-ctl refresh`
- Keep hook coverage in `src/scripts/xwmux-tmux-conf.sh` broad initially; all hooks can trigger the same refresh path
- Remove shell-side pane enumeration, sorting, xargs fan-out, etc.

## Suggested details
- Do not try to perfectly minimize hooks yet
- Redundant hooks are acceptable because refreshes should now be complete and idempotent

## Validation
Run at least:
- `python3 tests/run_all.py`
- or, if too expensive while iterating, the focused tests from earlier PRs plus `test_prefix.py`

---

# PR 7: Remove old `TMUX_POSITION` push path

## Goal
Finish the transition and delete the obsolete incremental protocol.

## Main changes
- Remove `TMUX_POSITION` handling from `WMInstance`
- Remove `NotifyTmuxPosition` command from `src/xwmux-utils/xwmux-ctl.cpp`
- Remove obsolete message helpers from `src/xwmux/ipc.h`
- Delete dead shell-side reporting logic references

## Suggested details
- Update logging and debug helpers accordingly
- Re-check `dump-state` output remains useful for debugging

## Validation
Run at least:
- `python3 tests/run_all.py`

---

# PR 8: Cleanup / simplify state ownership after refresh migration

## Goal
Simplify code now that refresh owns tmux-derived state transitions.

## Main changes
Target likely cleanup areas:
- reduce responsibilities inside `TmuxXWindowMapping`
- simplify `WMInstance` helper structure
- remove transitional duplication
- possibly move more side-effect logic out of mapping/data types

## Optional follow-ups in this PR or later
- clean up naming/typos
- reduce command framework complexity in `xwmux-ctl`
- simplify `write_state_json()` ownership if appropriate

## Validation
- `python3 tests/run_all.py`
- manual headless checks for focus/zoom/session switching if behavior was touched

---

## Follow-up after these PRs: sync / sequence numbers

**Do not implement in the PRs above unless necessary.**

Once refresh is complete and convergent, add:
- sequence numbers / sync protocol
- test-harness helpers to wait for refresh completion

At that point sync semantics should be defined in terms of completed refreshes, not hook counts.

---

## Behavior / invariant notes for whoever implements this

### Refresh completeness invariant
After one completed refresh, WM state should reflect current tmux state for all tmux-derived behavior.

### Convergence invariant
Running refresh twice without intervening tmux/X changes must not change effective state.

### Duplicate-hook invariant
Duplicate hook notifications must not require counting. Extra refreshes may happen, but they should be harmless.

### Pending-window invariant
Pending GUI windows must not already be bound into the mapping.

### Mapping invariant
A bound X window must map to exactly one tmux pane.

### Root-terminal invariant
The root terminal must never be treated as a normal GUI bound window.

---

## Risks / pitfalls to watch

### 1. Deterministic attachment ordering
Snapshot-based attach logic can accidentally become nondeterministic if attachable panes are iterated in hash-map order. Sort or otherwise define order explicitly.

### 2. Hidden coupling to old incremental logic
If behavior still depends on partial old `TMUX_POSITION` state, refresh is not yet complete.

### 3. Overloading `TmuxXWindowMapping` further
Avoid deepening the current model/side-effect mixing. Try not to add more focus/show/hide behavior there.

### 4. Prefix/override behavior
It may be tempting to redesign prefix behavior during this refactor. Avoid broadening scope too much unless required for correctness.

### 5. Script/protocol drift
Keep `xwmux-report.sh`, `xwmux-tmux-conf.sh`, `ipc.h`, and `xwmux-ctl.cpp` aligned during the transition.

---

## Existing issues worth noticing while in the area

These are not necessarily part of the refresh refactor, but they are worth being aware of:
- `assert(ev.type = ClientMessage);` in `src/xwmux/ipc.h` is an assignment bug
- `_XW_RESOUTION` typo in atom name
- `kill_ophans` typo
- `m_overriden` typo
- `xwmux-report.sh` currently references `tmux-focus`, which should be rechecked during transition
- `xwmux-launch-term.sh` terminal-selection line looks suspicious and should be reviewed separately if touched

Only fix these in the relevant PR if it helps and does not create too much unrelated noise.

---

## Test strategy

Follow repository guidance in `AGENTS.md`:
- prefer building and testing behavior, not only reasoning from code
- prefer headless integration environment

Build:
```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

Preferred targeted tests during iteration:
```bash
python3 tests/test_dump_state.py
python3 tests/test_focus.py
python3 tests/test_prefix.py
python3 tests/test_session_switching.py
python3 tests/test_closing.py
```

Broader validation:
```bash
python3 tests/run_all.py
```

For ad hoc investigation, use the existing harness in `tests/xwmux_test_harness.py`.

---

## Success criteria for the refresh refactor phase

This phase is successful when:
- hooks/scripts no longer push pane-by-pane tmux state
- `REFRESH` exists and is used as the internal hook-triggered protocol primitive
- WM coalesces adjacent refresh requests
- WM pulls tmux snapshot in C++
- a single refresh reconciles active pane/workspace, visibility, geometry, and pending GUI attachment correctly
- old `TMUX_POSITION` flow is removed
- focused integration tests pass
- code/event flow is simpler and easier to reason about than before
