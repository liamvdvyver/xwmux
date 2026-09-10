# Testing and inspection

The integration suite uses an isolated nested X server and tmux server, so it
can observe real X focus and tmux behavior without affecting the desktop.

## Build and run

From the repository root:

```sh
cmake -S . -B build
cmake --build build -j"$(nproc)"
python3 tests/run_all.py
```

If dependencies are unavailable, enter the Nix development environment first
(`nix develop`). Focused integration tests include:

```sh
python3 tests/test_dump_state.py
python3 tests/test_prefix.py
python3 tests/test_sync.py
python3 tests/test_focus.py
```

The tests skip when their headless prerequisites are not available.

## Harness model

`tests/xwmux_test_harness.py` starts `src/scripts/xwmux-xephyr-test.sh`, which
creates a unique tmux socket, launches a clean dedicated tmux server with
`src/scripts/xwmux-test.tmux.conf`, and starts xwmux in a nested display. The
harness exposes the nested environment, can create GUI windows and send keys
with `xdotool`, and queries tmux through that dedicated socket.

Useful observations are deliberately from separate layers:

- `xwmux-ctl dump-state` — xwmux's mapping, pending state, and prefix grab;
- `xdotool getwindowfocus` — actual X input focus;
- tmux `list-panes`, `list-windows`, and `display-message -p '#{client_prefix}'`
  — tmux's layout, selected pane/window, and prefix state.

Comparing those layers is important: a tmux hook is asynchronous relative to
X event processing, so a tmux change may be visible before the WM has applied
its next snapshot.

## Waiting for a deterministic refresh

The harness's `wait_until_wm_synced()` sends a monotonically increasing
`xwmux-ctl sync` request and waits for root property `_XW_SYNC_SEQ` to equal
the sequence. The WM processes the request by refreshing its tmux snapshot and
then acknowledging it. Use that when an assertion needs the effects of an
already-issued tmux command; use normal polling when testing the hook path
itself.

## Ad hoc session

```sh
python3 - <<'PY'
import json
import sys
sys.path.insert(0, 'tests')
from xwmux_test_harness import XwmuxTestHarness

h = XwmuxTestHarness()
try:
    print(json.dumps(h.start(), indent=2, sort_keys=True))
    h.wait_until_ready()
    print(json.dumps(h.dump_state(), indent=2, sort_keys=True))
finally:
    h.stop()
PY
```

See [IPC and tmux integration](ipc-and-tmux.md) for the state and sync
protocol and [Event flows](event-flows.md) for what a refresh applies.
