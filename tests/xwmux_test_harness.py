#!/usr/bin/env python3

"""Shared helpers for xwmux integration tests.

This harness is responsible for starting the nested Xephyr-based test instance,
tracking child processes created by a test, and providing small utilities for
observing xwmux state through both tmux and X11-facing tools.
"""

import json
import os
import select
import signal
import subprocess
import sys
import tempfile
import threading
import time
from collections import deque
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build"
SCRIPTS_DIR = ROOT / "src" / "scripts"
LAUNCHER = SCRIPTS_DIR / "xwmux-xephyr-test.sh"
XWMUX_CTL = BUILD_DIR / "xwmux-ctl"
TTY_KEY_LOGGER = ROOT / "tests" / "tty_key_logger.py"


class XWindowTreeError(RuntimeError):
    """Raised when the test harness cannot inspect the X window tree."""


class XwmuxTestHarness:
    """Utility object for end-to-end xwmux tests.

    Main responsibilities:
    - launch and stop an isolated xwmux instance inside Xephyr
    - expose the nested DISPLAY and tmux socket through ``self.test_env``
    - run helper commands against the isolated tmux server
    - inspect WM state via ``xwmux-ctl dump-state``
    - query actual X focus via ``xdotool``
    """

    def __init__(self):
        self.launcher = None
        self.launcher_log_thread = None
        self.test_env = None
        self.processes = []
        self.action_log = deque(maxlen=200)
        self.launcher_logs = deque(maxlen=400)

    @staticmethod
    def require_program(name):
        """Fail if an external program needed by the tests is unavailable."""
        if subprocess.run(["sh", "-c", f"command -v {name} >/dev/null"]).returncode != 0:
            raise RuntimeError(f"{name} is not available in PATH")

    @staticmethod
    def wants_verbose_logs():
        """True when the current test invocation asked for verbose output."""
        return any(arg in {"-v", "--verbose"} for arg in sys.argv[1:])

    @staticmethod
    def check_prerequisites():
        """Validate that the current environment can run integration tests."""
        if "DISPLAY" not in os.environ:
            XwmuxTestHarness.require_program("Xvfb")
        if not LAUNCHER.exists():
            raise RuntimeError(f"missing launcher: {LAUNCHER}")
        if not XWMUX_CTL.exists():
            raise RuntimeError(f"missing binary: {XWMUX_CTL}")

        for program in ["xmessage", "xdotool", "tmux", "python3", "xterm", "xwininfo", "Xephyr"]:
            XwmuxTestHarness.require_program(program)

    def _record_action(self, action, details=None):
        entry = {
            "ts": round(time.time(), 3),
            "action": action,
        }
        if details is not None:
            entry["details"] = details
        self.action_log.append(entry)

    def _capture_launcher_line(self, line):
        self.launcher_logs.append(line.rstrip("\n"))

    def start(self):
        """Start the nested xwmux test instance and capture its environment.

        Returns a dict parsed from launcher startup output. The most important
        keys are ``DISPLAY`` and ``XWMUX_TMUX_SOCKET``.
        """
        launcher_env = os.environ.copy()
        launcher_env["PATH"] = f"{BUILD_DIR}:{SCRIPTS_DIR}:{launcher_env.get('PATH', '')}"

        launcher_cmd = [str(LAUNCHER)]
        if self.wants_verbose_logs():
            launcher_cmd.append("-v")

        self._record_action("start", {"command": launcher_cmd})
        self.launcher = subprocess.Popen(
            launcher_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=launcher_env,
            start_new_session=True,
        )

        startup = self._read_startup_env()
        self.test_env = launcher_env.copy()
        self.test_env["DISPLAY"] = startup["DISPLAY"]
        self.test_env["XWMUX_TMUX_SOCKET"] = startup["XWMUX_TMUX_SOCKET"]
        self._record_action("startup-env", startup)

        self.launcher_log_thread = threading.Thread(
            target=self._forward_launcher_logs,
            daemon=True,
        )
        self.launcher_log_thread.start()

        return startup

    def _forward_launcher_logs(self):
        """Capture launcher output after startup, optionally mirroring to stderr."""
        if self.launcher is None or self.launcher.stdout is None:
            return

        for line in self.launcher.stdout:
            self._capture_launcher_line(line)
            if self.wants_verbose_logs():
                sys.stderr.write(line)
                sys.stderr.flush()

    def stop(self):
        """Terminate all test-owned processes and shut down xwmux cleanly."""
        self._record_action("stop")
        for proc in reversed(self.processes):
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=2)
        self.processes.clear()

        if self.test_env is not None:
            subprocess.run([str(XWMUX_CTL), "exit"], env=self.test_env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        if self.launcher is not None:
            try:
                os.killpg(self.launcher.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass

            try:
                self.launcher.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(self.launcher.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                self.launcher.wait(timeout=5)

        if self.launcher_log_thread is not None:
            self.launcher_log_thread.join(timeout=1)
            self.launcher_log_thread = None

    def _read_startup_env(self):
        """Read launcher stdout until nested DISPLAY and tmux socket are known."""
        startup = {}
        deadline = time.time() + 20

        while time.time() < deadline:
            ready, _, _ = select.select([self.launcher.stdout], [], [], 0.5)
            if not ready:
                if self.launcher.poll() is not None:
                    remaining = self.launcher.stdout.read() or ""
                    for line in remaining.splitlines():
                        self._capture_launcher_line(line)
                    raise RuntimeError(
                        "launcher exited before startup completed\n"
                        f"stdout:\n{remaining}"
                    )
                continue

            line = self.launcher.stdout.readline()
            if not line:
                continue

            self._capture_launcher_line(line)
            if self.wants_verbose_logs():
                sys.stderr.write(line)
                sys.stderr.flush()

            line = line.rstrip("\n")
            if line.startswith("  ") and "=" in line:
                key, value = line.strip().split("=", 1)
                startup[key] = value
                if "DISPLAY" in startup and "XWMUX_TMUX_SOCKET" in startup:
                    return startup

        raise RuntimeError("timed out waiting for launcher startup output")

    def _run_command(self, argv, *, env=None, text=True, stderr=None, check=True):
        return subprocess.run(
            argv,
            env=env or self.test_env,
            text=text,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL if stderr is None else stderr,
            check=check,
        )

    def _safe_command_output(self, argv, *, env=None, text=True):
        try:
            proc = self._run_command(argv, env=env, text=text, stderr=subprocess.STDOUT, check=False)
        except Exception as err:  # pragma: no cover - diagnostics only
            return f"<command failed to run: {err}>"

        output = proc.stdout if text else repr(proc.stdout)
        if proc.returncode != 0:
            return f"<exit {proc.returncode}>\n{output}"
        return output

    def dump_state(self):
        """Return the current WM state as parsed JSON from ``xwmux-ctl``."""
        self._record_action("dump-state")
        last_output = ""
        for _ in range(3):
            output = self._safe_command_output([str(XWMUX_CTL), "dump-state"])
            last_output = output
            try:
                return json.loads(output)
            except json.JSONDecodeError:
                time.sleep(0.05)
        raise AssertionError(f"failed to decode xwmux dump-state output: {last_output!r}")

    def launch_gui_window(self, message="xwmux test window", title="xwmux-test-gui"):
        """Launch a simple GUI client inside the nested X server.

        The returned process is tracked automatically and will be terminated by
        ``stop()``.
        """
        self._record_action("launch-gui-window", {"title": title, "message": message})
        proc = subprocess.Popen(
            ["xmessage", "-title", title, message],
            env=self.test_env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.processes.append(proc)
        return proc

    def launch_and_wait_for_gui_pane(
        self,
        message="xwmux test window",
        title="xwmux-test-gui",
        state_message="state dump never showed a GUI window",
        focus_message="GUI window wasn't focused after launching",
        x_focus_message="GUI window was not X-focused after launching",
    ):
        """Launch a GUI window and wait until xwmux reports it active and focused."""
        self.launch_gui_window(message=message, title=title)

        self.wait_for_state(
            self.state_has_gui_window,
            message=state_message,
        )

        gui_pane = self.wait_for(
            lambda: self.active_gui_pane(self.dump_state()),
            message=focus_message,
        )
        self.wait_for(
            lambda: self.focused_window_id() == gui_pane["x_window"],
            message=x_focus_message,
        )

        return gui_pane

    def launch_key_logger_window(self, title="xwmux-key-logger"):
        """Launch a GUI terminal app that records raw key bytes to a temp file.

        TODO: Switch this test helper back to xterm once synthetic prefix
        passthrough is implemented with XTEST and xterm is supported on that
        path.
        """
        log_path = Path(tempfile.mkstemp(prefix="xwmux-keylog.", suffix=".bin")[1])
        self._record_action("launch-key-logger", {"title": title, "log_path": str(log_path)})
        proc = subprocess.Popen(
            ["kitty", "--name", title, "-e", "python3", str(TTY_KEY_LOGGER), str(log_path)],
            env=self.test_env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.processes.append(proc)
        return proc, log_path

    def tmux(self, *args):
        """Run a tmux command against the isolated test server and return stdout."""
        self._record_action("tmux", list(args))
        return subprocess.check_output(
            ["tmux", "-S", self.test_env["XWMUX_TMUX_SOCKET"], *args],
            env=self.test_env,
            text=True,
        )

    def list_panes(self):
        """Return a simple parsed view of tmux panes across all windows."""
        output = self.tmux("list-panes", "-a", "-F", "#{window_id} #{pane_id} #{pane_active}")
        panes = []
        for line in output.splitlines():
            window_id, pane_id, pane_active = line.split()
            panes.append(
                {
                    "window": int(window_id[1:]),
                    "pane": int(pane_id[1:]),
                    "active": bool(int(pane_active)),
                }
            )
        return panes

    def create_window(self):
        """Create a detached tmux window and return its numeric window id."""
        output = self.tmux("new-window", "-d", "-P", "-F", "#{window_id}")
        return int(output.strip()[1:])

    def select_window(self, window_id):
        """Select a tmux window by numeric id."""
        self.tmux("select-window", "-t", f"@{window_id}")

    def get_gui_panes(self, state):
        """Extract panes from a state dump that currently host X GUI windows."""
        gui_panes = []
        for tm_window, workspace in state.get("workspaces", {}).items():
            for tm_pane, pane_state in workspace.get("panes", {}).items():
                gui_panes.append(
                    {
                        "window": int(tm_window),
                        "pane": int(tm_pane),
                        "x_window": int(pane_state["window"]),
                    }
                )
        return gui_panes

    def state_has_gui_window(self, state):
        """Convenience predicate: true when any pane is mapped to a GUI window."""
        return bool(self.get_gui_panes(state))

    def active_gui_pane(self, state):
        """Return the active GUI pane entry when the active tmux pane hosts a GUI."""
        active = state.get("active")
        if not active:
            return None
        for gui_pane in self.get_gui_panes(state):
            if gui_pane["window"] == active.get("window") and gui_pane["pane"] == active.get("pane"):
                return gui_pane
        return None

    def find_gui_pane(self, state, *, x_window=None, pane=None):
        """Find a GUI pane in a state snapshot by X window id and/or pane id."""
        for gui_pane in self.get_gui_panes(state):
            if x_window is not None and gui_pane["x_window"] != x_window:
                continue
            if pane is not None and gui_pane["pane"] != pane:
                continue
            return gui_pane
        return None

    def wait_for_gui_pane(self, *, x_window=None, pane=None, message="GUI pane was not found"):
        """Wait until a GUI pane matching the requested identity exists."""
        return self.wait_for(
            lambda: self.find_gui_pane(self.dump_state(), x_window=x_window, pane=pane),
            message=message,
        )

    def wait_until_ready(self):
        """Wait until startup has produced a root terminal, layout, and prefix state."""
        self.wait_for_state(
            lambda s: s.get("layout_initialized") and s.get("prefix", {}).get("initialized") and s.get("root_terminal_window"),
            message="xwmux test harness never reached ready state",
        )
        root_window = self.root_terminal_window_id()
        self.wait_for(
            lambda: self.focused_window_id() == root_window,
            message="root terminal was not focused after startup",
        )

    def root_terminal_window_id(self):
        """Return the X window id of the xwmux root terminal window."""
        def lookup():
            try:
                output = subprocess.check_output(
                    ["xdotool", "search", "--class", "xwmux_root"],
                    env=self.test_env,
                    text=True,
                    stderr=subprocess.DEVNULL,
                )
            except subprocess.CalledProcessError:
                return None
            return int(output.splitlines()[-1])

        return self.wait_for(lookup, message="root terminal window did not appear")

    def focused_window_id(self):
        """Return the currently focused X window id in the nested display."""
        output = subprocess.check_output(["xdotool", "getwindowfocus"], env=self.test_env, text=True)
        return int(output.strip())

    def kill_window(self, window_id):
        """Ask X to destroy a window by id."""
        self._record_action("kill-window", {"window_id": window_id})
        subprocess.check_call(["xdotool", "windowkill", str(window_id)], env=self.test_env)

    def root_child_stack(self):
        """Return root child windows in bottom-to-top stacking order."""
        output = subprocess.check_output(
            ["xwininfo", "-root", "-children"],
            env=self.test_env,
            text=True,
        )

        children = []
        in_children_section = False
        for raw_line in output.splitlines():
            line = raw_line.rstrip()
            stripped = line.lstrip()

            if stripped.endswith("children:") or stripped.endswith("child windows:"):
                in_children_section = True
                continue
            if not in_children_section:
                continue

            if not stripped:
                continue
            if not stripped.startswith("0x"):
                continue

            window_id_text = stripped.split()[0]
            try:
                children.append(int(window_id_text, 16))
            except ValueError as err:
                raise XWindowTreeError(
                    f"could not parse xwininfo child line: {raw_line!r}"
                ) from err

        if not children:
            raise XWindowTreeError(
                f"xwininfo returned no root children; output was:\n{output}"
            )

        # Lines are read top (highest in stack) to bottom (lowest in stack) of
        # output, so need reverse
        children.reverse()
        return children

    def stack_index(self, window_id):
        """Return the root-child stack index for a window, bottom-most first."""
        stack = self.root_child_stack()
        try:
            return stack.index(window_id)
        except ValueError as err:
            raise AssertionError(
                f"window {window_id} not present in root child stack {stack}"
            ) from err

    def is_below(self, lower_window_id, upper_window_id):
        """True when ``lower_window_id`` is below ``upper_window_id``."""
        try:
            return self.stack_index(lower_window_id) < self.stack_index(upper_window_id)
        except AssertionError:
            return False

    def window_size(self, window_id):
        """Return ``(width, height)`` for an X window in the nested display."""
        output = subprocess.check_output(
            ["xdotool", "getwindowgeometry", "--shell", str(window_id)],
            env=self.test_env,
            text=True,
        )
        values = {}
        for line in output.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                values[key] = value
        return int(values["WIDTH"]), int(values["HEIGHT"])

    def send_key(self, key):
        """Send a key chord via xdotool into the nested display."""
        self._record_action("send-key", {"key": key})
        subprocess.check_call(["xdotool", "key", key], env=self.test_env)

    def tmux_client_prefix_active(self):
        """Return whether tmux reports the attached client is in prefix mode."""
        output = self.tmux("display-message", "-p", "#{client_prefix}")
        return output.strip() == "1"

    def read_key_log(self, log_path):
        """Return raw bytes collected by the GUI key logger so far."""
        if not Path(log_path).exists():
            return b""
        return Path(log_path).read_bytes()

    def diagnostic_snapshot(self):
        """Collect best-effort state useful for debugging flaky failures."""
        snapshot = {
            "action_log": list(self.action_log),
            "launcher_logs": list(self.launcher_logs),
            "env": {
                "DISPLAY": None if self.test_env is None else self.test_env.get("DISPLAY"),
                "XWMUX_TMUX_SOCKET": None if self.test_env is None else self.test_env.get("XWMUX_TMUX_SOCKET"),
            },
        }

        if self.test_env is None:
            return snapshot

        raw_state = self._safe_command_output([str(XWMUX_CTL), "dump-state"])
        try:
            snapshot["dump_state"] = json.loads(raw_state)
        except json.JSONDecodeError:
            snapshot["dump_state_error"] = raw_state

        snapshot["focused_window"] = self._safe_command_output(["xdotool", "getwindowfocus"]).strip()
        snapshot["tmux_client_prefix"] = self._safe_command_output(
            ["tmux", "-S", self.test_env["XWMUX_TMUX_SOCKET"], "display-message", "-p", "#{client_prefix}"]
        ).strip()
        snapshot["tmux_list_panes"] = self._safe_command_output(
            ["tmux", "-S", self.test_env["XWMUX_TMUX_SOCKET"], "list-panes", "-a", "-F",
             "#{session_name} #{window_id} #{pane_id} active=#{pane_active} dead=#{pane_dead} zoomed=#{window_zoomed_flag} #{pane_left} #{pane_top} #{pane_width} #{pane_height}"]
        )
        snapshot["tmux_list_windows"] = self._safe_command_output(
            ["tmux", "-S", self.test_env["XWMUX_TMUX_SOCKET"], "list-windows", "-a", "-F",
             "#{session_name} #{window_id} active=#{window_active} panes=#{window_panes} zoomed=#{window_zoomed_flag} name=#{window_name}"]
        )
        snapshot["root_child_stack"] = self._safe_command_output(["xwininfo", "-root", "-children"])
        snapshot["root_term_search"] = self._safe_command_output(["xdotool", "search", "--class", "xwmux_root"])
        return snapshot

    def format_diagnostics(self):
        return json.dumps(self.diagnostic_snapshot(), indent=2, sort_keys=True)

    def wait_for(self, predicate, timeout=10, interval=0.2, message="condition was not met"):
        """Poll ``predicate`` until it returns a truthy value, then return it."""
        deadline = time.time() + timeout
        last_value = None
        while time.time() < deadline:
            last_value = predicate()
            if last_value:
                return last_value
            time.sleep(interval)

        diagnostics = self.format_diagnostics()
        raise AssertionError(
            f"{message}; last value: {last_value}\nDiagnostics:\n{diagnostics}"
        )

    def wait_for_state(self, predicate, timeout=10, interval=0.2, message="state condition was not met"):
        """Poll ``dump_state()`` until ``predicate(state)`` becomes true.

        On success this returns the state object that satisfied the predicate.
        On failure the raised assertion includes the last observed state.
        """
        last_state = None

        def check_state():
            nonlocal last_state
            last_state = self.dump_state()
            if predicate(last_state):
                return last_state
            return None

        try:
            return self.wait_for(check_state, timeout=timeout, interval=interval,
                                 message=message)
        except AssertionError as err:
            raise AssertionError(f"{err}\nLast observed state:\n{json.dumps(last_state, indent=2, sort_keys=True)}") from err
