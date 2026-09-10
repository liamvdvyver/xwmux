# Action Decision Register

This register records the decisions and delivered work from the snapshot-refresh,
synchronization, and prefix-input refactor.

## Operating decisions

| Decision | Rationale | Outcome |
| --- | --- | --- |
| Use snapshot reconciliation rather than incremental tmux hook messages. | A complete tmux snapshot is convergent and makes duplicate/delayed hooks harmless. | Hooks request refresh; xwmux pulls and reconciles tmux state. |
| Keep xwmux single-threaded. | X11 and WM state remain easier to reason about with serialized event handling. | Refresh and IPC work are handled in the existing event loop. |
| Keep `TmuxXWindowMapping` model-oriented and put X11 side effects in `WMInstance`. | Mapping policy should not own focus, visibility, geometry, or grabs. | X11 application logic was progressively moved to `WMInstance`. |
| Preserve conservative pending-window attachment policy. | Broadening attachment eligibility previously regressed prefix/focus behavior. | Only focused, unbound panes with `pane_pid == 0` are eligible. |
| Treat refresh as idempotent; do not count hook deliveries. | Hooks can be duplicate or delayed. | Adjacent refresh requests are coalesced and each refresh reconciles a full snapshot. |
| Add an acknowledged sync barrier. | Tests need a deterministic point after xwmux has processed a tmux snapshot. | `SYNC(sequence)` refreshes then publishes `_XW_SYNC_SEQ` on the root window. |
| Do not replace sleeps until a test proves the barrier/input path is ready. | Refresh completion alone originally did not prove that injected tmux input was ready. | The prefix sleep remained until a no-delay regression passed. |
| Replay an already grabbed prefix event with `ReplayKeyboard`, not a second XTEST chord. | The real key is held by a synchronous passive grab. A second injected chord can race or duplicate input. | `XAllowEvents(..., ReplayKeyboard, ...)` replays the original event once after its focus target changes. |

## Delivered changes

### Snapshot refresh and reconciliation

- Added C++ tmux snapshot collection and `REFRESH` IPC.
- Changed tmux hooks to request refresh only; removed live `TMUX_POSITION` dispatch.
- Coalesced adjacent refresh requests.
- Reconciled snapshots in explicit phases: orphan removal, pane movement, focused-pane attachment/focus, then geometry.
- Moved visibility, X focus, prefix grabs, mapping, resizing, close requests, and geometry application into `WMInstance`.
- Split pending attachment into queue consumption, eligibility, and attachment operations.
- Updated tmux compatibility: snapshot captures `pane_pid`; pending panes are identified by `pane_pid == 0` rather than `pane_dead`.
- Fixed attached-client active window/pane tracking across tmux session switches.

### Synchronization and test support

- Added `SYNC` IPC, `xwmux-ctl sync <sequence>`, `_XW_SYNC`, and `_XW_SYNC_SEQ` acknowledgment properties.
- Added `WMInstance::acknowledge_sync(long)`.
- Added harness helpers: `wm_sync_sequence()`, `wait_until_wm_synced()`, and `tmux_and_wait(...)`.
- Added regression coverage for tmux window/session switching, sync after window switching, and preserving prefix override through a duplicate refresh.

### Prefix input delivery

- Removed out-of-band `TmuxClient::send_prefix()` / `tmux send-keys -K` injection.
- Removed the GUI double-prefix `XSendEvent` passthrough.
- After focus moves to the terminal or back to the GUI, xwmux synchronizes the focus request and calls `XAllowEvents(..., ReplayKeyboard, ...)`.
- This delivers the original captured chord to the new focus target as a normal key event. The double-prefix path now produces control byte `0x02`, rather than the old synthetic plain `b` behavior.
- Replaced the former two-second prefix-test sleep with `wait_until_wm_synced()`.
- Added a split -> sync -> immediate prefix -> `!` regression with no timing delay.

## Commit record

| Commit | Change |
| --- | --- |
| `c5303f0` | Snapshot-based refresh scaffolding |
| `43a9e9c` | Simplified WM refresh handling |
| `e6c5296` | Removed legacy tmux-position IPC flow |
| `53a3125` | Extracted pane application helpers |
| `7e15864` | Extracted orphan-cleanup phase |
| `b2fdc46` | Clarified reconciliation phases |
| `f72ec4f` | Extracted active-focus decisions |
| `63b3472` | Fixed active-pane tracking across tmux sessions |
| `9a41be4` | Separated geometry decisions from application |
| `d2cc837` | Wrapped visibility and focus application |
| `bbbdde5` | Extracted focus-policy helpers |
| `6415afc` | Extracted pending-attachment policy |
| `6a0e94a` | Updated Nix development inputs |
| `45cf107` | Attached pending windows to zero-PID panes |
| `147bd96` | Moved active-pane effects into WMInstance |
| `54830cd` | Moved mapping X11 effects into WMInstance |
| `8b70080` | Split pending-window attachment operations |
| `3ba7c2c` | Documented sync-barrier plan |
| `867a476` | Phased snapshot pane reconciliation |
| `607e998` | Added sync completion acknowledgments |
| `941a4ef` | Added refresh-barrier test helpers |
| `6be2541` | Documented initial sync limitations |
| `ab76674` | Documented sync-barrier investigation |
| `3c9dcb6` | Tested prefix override preservation through refresh |
| `098d532` | Recorded split-prefix timing findings |
| `d5e8145` | Replayed captured keys after focus changes |
| `12348cd` | Documented replay-based prefix fix |

## Validation record

Latest validation after the prefix replay change:

- `env -u DISPLAY python3 tests/test_prefix.py`: 5 passed.
- `env -u DISPLAY python3 tests/test_sync.py`: 3 passed.
- `env -u DISPLAY python3 tests/run_all.py`: 22 passed.

The build succeeds. Existing `src/xwmux/layout.h` `-Wmaybe-uninitialized` warnings remain and are unrelated to this work.

## Remaining follow-ups

- Clarify and, only if needed, further refactor pending-window attachment invariants without broadening eligibility.
- Replace other justified polling/sleeps with the sync barrier and state assertions.
- Continue reducing remaining `WMInstance`/mapping ownership overlap.
- Consider diff-based presentation application if redundant focus/map/unmap operations become observable.
