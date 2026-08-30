#!/usr/bin/env python3

import subprocess
import sys
import unittest

from xwmux_test_harness import XwmuxTestHarness


class ResolutionChangeIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            XwmuxTestHarness.check_prerequisites()
            XwmuxTestHarness.require_program("xrandr")
        except RuntimeError as err:
            raise unittest.SkipTest(str(err))

    def setUp(self):
        self.harness = XwmuxTestHarness()
        self.harness.start()
        self.harness.wait_until_ready()

    def tearDown(self):
        self.harness.stop()

    def _set_resolution(self, mode):
        proc = subprocess.run(
            ["xrandr", "-s", mode],
            env=self.harness.test_env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if proc.returncode != 0:
            raise AssertionError(
                f"xrandr -s {mode} failed with exit {proc.returncode}:\n{proc.stdout}"
            )

    def _launcher_logs_contain_x_errors(self):
        joined = "\n".join(self.harness.launcher_logs)
        return "BadWindow" in joined or "BadValue" in joined

    def test_resolution_change_reopens_root_terminal_without_stealing_gui_focus(self):
        gui_pane = self.harness.launch_and_wait_for_gui_pane(
            title="xwmux-resolution-change-gui"
        )
        original_root = self.harness.root_terminal_window_id()

        self._set_resolution("1024x768")

        self.harness.wait_for(
            lambda: self.harness.window_size(gui_pane["x_window"]) == (1024, 738),
            message="GUI window did not resize to the new display resolution",
        )
        self.harness.wait_for_state(
            lambda s: pane_state
            if (
                (pane_state := s.get("workspaces", {})
                    .get(str(gui_pane["window"]), {})
                    .get("panes", {})
                    .get(str(gui_pane["pane"])))
                and not pane_state.get("hidden")
            )
            else None,
            message="GUI pane became hidden after changing the display resolution",
        )
        self.harness.wait_for(
            lambda: self.harness.root_terminal_window_id(),
            message="root terminal did not come back after changing the display resolution",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message=(
                "changing the display resolution reopened the root terminal but did not restore "
                "focus to the active GUI window"
            ),
        )
        self.assertFalse(
            self._launcher_logs_contain_x_errors(),
            "changing the display resolution produced BadWindow/BadValue X11 errors",
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ResolutionChangeIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
