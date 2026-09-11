# AGENTS

Guidance for coding agents working in this repository.

## Development workflow

- Prefer to test changes and behavior theories instead of reasoning only from code.
- When investigating GUI/tmux/X11 behavior, prefer the headless integration environment so you do not interrupt the user's desktop with real GUI windows.
- If you do need to inspect behavior manually, use the isolated test harness first.

## Build

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

If the project does not build because libraries or tools are missing, activate the Nix development environment first, then build again:

```bash
nix develop
```

Expected binaries after build:

- `build/xwmux`
- `build/xwmux-ctl`

## Preferred test paths

Run integration tests only with the safe invocation (including focused runs):

```bash
env -u DISPLAY -u XAUTHORITY -u TMUX python3 tests/run_all.py
```

```bash
env -u DISPLAY -u XAUTHORITY -u TMUX python3 tests/test_dump_state.py
env -u DISPLAY -u XAUTHORITY -u TMUX python3 tests/test_prefix.py
```

The harness also strips these variables from its launcher and always creates
its own Xvfb parent display. Never run GUI tests against the developer's
`DISPLAY`, use `pkill`/`killall`, or run tmux cleanup without the harness's
private socket.

## Headless test environment

Use the existing nested Xephyr/Xvfb test environment through the Python harness in `tests/xwmux_test_harness.py`.

The harness can:

- start/stop an isolated xwmux instance
- create GUI windows inside the nested display
- send keypresses with `xdotool`
- inspect tmux state
- inspect xwmux state with `xwmux-ctl dump-state`

Minimal ad hoc example:

```bash
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

## Behavioral investigation policy

When you suspect a behavior change or bug:

1. build the repo
2. reproduce it in the headless harness when possible
3. inspect state with `xwmux-ctl dump-state`, tmux queries, and X focus/window helpers
4. only then change code
5. re-run targeted tests and any relevant broader tests
