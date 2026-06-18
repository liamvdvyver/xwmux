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
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build"
SCRIPTS_DIR = ROOT / "src" / "scripts"
LAUNCHER = SCRIPTS_DIR / "xwmux-xephyr-test.sh"
XWMUX_CTL = BUILD_DIR / "xwmux-ctl"
TTY_KEY_LOGGER = ROOT / "tests" / "tty_key_logger.py"


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
            raise RuntimeError("DISPLAY is not set")
        if not LAUNCHER.exists():
            raise RuntimeError(f"missing launcher: {LAUNCHER}")
        if not XWMUX_CTL.exists():
            raise RuntimeError(f"missing binary: {XWMUX_CTL}")

        for program in ["xmessage", "xdotool", "tmux", "python3", "xterm"]:
            XwmuxTestHarness.require_program(program)

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

        if self.wants_verbose_logs():
            self.launcher_log_thread = threading.Thread(
                target=self._forward_launcher_logs,
                daemon=True,
            )
            self.launcher_log_thread.start()

        return startup

    def _forward_launcher_logs(self):
        """Forward launcher output to this process' stderr after startup."""
        if self.launcher is None or self.launcher.stdout is None:
            return

        for line in self.launcher.stdout:
            sys.stderr.write(line)
            sys.stderr.flush()

    def stop(self):
        """Terminate all test-owned processes and shut down xwmux cleanly."""
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
                    raise RuntimeError(
                        "launcher exited before startup completed\n"
                        f"stdout:\n{remaining}"
                    )
                continue

            line = self.launcher.stdout.readline()
            if not line:
                continue

            line = line.rstrip("\n")
            if line.startswith("  ") and "=" in line:
                key, value = line.strip().split("=", 1)
                startup[key] = value
                if "DISPLAY" in startup and "XWMUX_TMUX_SOCKET" in startup:
                    return startup

        raise RuntimeError("timed out waiting for launcher startup output")

    def dump_state(self):
        """Return the current WM state as parsed JSON from ``xwmux-ctl``."""
        output = subprocess.check_output([str(XWMUX_CTL), "dump-state"], env=self.test_env, text=True)
        return json.loads(output)

    def launch_gui_window(self, message="xwmux test window", title="xwmux-test-gui"):
        """Launch a simple GUI client inside the nested X server.

        The returned process is tracked automatically and will be terminated by
        ``stop()``.
        """
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

        state = self.wait_for_state(
            self.state_has_gui_window,
            message=state_message,
        )

        gui_pane = self.get_gui_panes(state)[0]

        self.wait_for_state(
            lambda s: s.get("active")
            == {"window": gui_pane["window"], "pane": gui_pane["pane"]},
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

    def wait_for(self, predicate, timeout=10, interval=0.2, message="condition was not met"):
        """Poll ``predicate`` until it returns a truthy value, then return it."""
        deadline = time.time() + timeout
        last_value = None
        while time.time() < deadline:
            last_value = predicate()
            if last_value:
                return last_value
            time.sleep(interval)
        raise AssertionError(f"{message}; last value: {last_value}")

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
            raise AssertionError(f"{err}; last state: {last_state}") from err
