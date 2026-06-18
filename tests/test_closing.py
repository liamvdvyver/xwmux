#!/usr/bin/env python3

import subprocess
import sys
import time
import unittest

from xwmux_test_harness import XWMUX_CTL, XwmuxTestHarness


class ClosingIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            XwmuxTestHarness.check_prerequisites()
        except RuntimeError as err:
            raise unittest.SkipTest(str(err))

    def setUp(self):
        self.harness = XwmuxTestHarness()
        self.harness.start()

        time.sleep(1.5)

    def tearDown(self):
        self.harness.stop()

    def _xwmux_ctl(self, *args):
        subprocess.check_call(
            [str(XWMUX_CTL), *args],
            env=self.harness.test_env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def _window_id_for_title(self, title):
        return self.harness.wait_for(
            lambda: self._search_window_by_title(title),
            message=f"window with title {title!r} did not appear",
        )

    def _search_window_by_title(self, title):
        try:
            output = subprocess.check_output(
                ["xdotool", "search", "--name", title],
                env=self.harness.test_env,
                text=True,
                stderr=subprocess.DEVNULL,
            )
        except subprocess.CalledProcessError:
            return None
        lines = [line.strip() for line in output.splitlines() if line.strip()]
        if not lines:
            return None
        return int(lines[-1])

    def _wait_for_gui_pane_for_title(self, title):
        x_window = self._window_id_for_title(title)
        state = self.harness.wait_for_state(
            lambda s: next(
                (
                    pane
                    for pane in self.harness.get_gui_panes(s)
                    if pane["x_window"] == x_window
                ),
                None,
            ),
            message=f"state never mapped GUI window titled {title!r}",
        )
        return next(
            pane for pane in self.harness.get_gui_panes(state) if pane["x_window"] == x_window
        )

    def _mapping_contains(self, state, *, pane=None, x_window=None):
        if pane is not None and str(pane) in state.get("inverse_tm_map", {}):
            return True
        if x_window is not None and str(x_window) in state.get("inverse_map", {}):
            return True
        for workspace in state.get("workspaces", {}).values():
            panes = workspace.get("panes", {})
            if pane is not None and str(pane) in panes:
                return True
            if x_window is not None:
                for pane_state in panes.values():
                    if int(pane_state["window"]) == x_window:
                        return True
        return False

    def _tmux_has_pane(self, pane):
        return any(tmux_pane["pane"] == pane for tmux_pane in self.harness.list_panes())

    def _assert_closed_everywhere(self, proc, gui_pane):
        self.harness.wait_for(
            lambda: proc.poll() is not None,
            message=f"GUI process for pane %{gui_pane['pane']} never exited",
        )
        self.harness.wait_for(
            lambda: not self._tmux_has_pane(gui_pane["pane"]),
            message=f"tmux pane %{gui_pane['pane']} was not removed",
        )
        self.harness.wait_for_state(
            lambda s: not self._mapping_contains(
                s, pane=gui_pane["pane"], x_window=gui_pane["x_window"]
            ),
            message=(
                f"mapping still referenced pane %{gui_pane['pane']} "
                f"or window {gui_pane['x_window']}"
            ),
        )

    def test_xwmux_ctl_kill_pane_focused_closes_window_and_clears_mapping(self):
        title = "xwmux-closing-focused"
        proc = self.harness.launch_gui_window(title=title)
        gui_pane = self._wait_for_gui_pane_for_title(title)

        self._xwmux_ctl("kill-pane", "focused")

        self._assert_closed_everywhere(proc, gui_pane)

    def test_xwmux_ctl_kill_pane_targeted_closes_requested_window_and_clears_mapping(self):
        title1 = "xwmux-closing-target-1"
        title2 = "xwmux-closing-target-2"

        proc1 = self.harness.launch_gui_window(title=title1)
        gui_pane1 = self._wait_for_gui_pane_for_title(title1)

        proc2 = self.harness.launch_gui_window(title=title2)
        gui_pane2 = self._wait_for_gui_pane_for_title(title2)

        self.assertNotEqual(gui_pane1["pane"], gui_pane2["pane"])
        self.harness.wait_for(
            lambda: proc1.poll() is None and proc2.poll() is None,
            message="one of the GUI processes exited before the test acted on it",
        )

        self._xwmux_ctl("kill-pane", f"%{gui_pane1['pane']}")

        self._assert_closed_everywhere(proc1, gui_pane1)

        self.harness.wait_for(
            lambda: proc2.poll() is None,
            message="the non-targeted GUI process exited unexpectedly",
        )
        remaining_state = self.harness.wait_for_state(
            lambda s: self._mapping_contains(
                s, pane=gui_pane2["pane"], x_window=gui_pane2["x_window"]
            ),
            message="the non-targeted GUI pane disappeared from the mapping",
        )
        self.assertTrue(
            self._mapping_contains(
                remaining_state, pane=gui_pane2["pane"], x_window=gui_pane2["x_window"]
            )
        )

    def test_killing_gui_program_directly_closes_tmux_pane_and_clears_mapping(self):
        title = "xwmux-closing-proc"
        proc = self.harness.launch_gui_window(title=title)
        gui_pane = self._wait_for_gui_pane_for_title(title)

        proc.terminate()

        self._assert_closed_everywhere(proc, gui_pane)

    def test_killing_tmux_pane_directly_closes_gui_process_and_clears_mapping(self):
        title = "xwmux-closing-tmux-pane"
        proc = self.harness.launch_gui_window(title=title)
        gui_pane = self._wait_for_gui_pane_for_title(title)

        self.harness.tmux("kill-pane", "-t", f"%{gui_pane['pane']}")

        self._assert_closed_everywhere(proc, gui_pane)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ClosingIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
