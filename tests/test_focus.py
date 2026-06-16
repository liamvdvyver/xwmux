#!/usr/bin/env python3

import sys
import time
import unittest

from xwmux_test_harness import XwmuxTestHarness


class FocusIntegrationTest(unittest.TestCase):
    def _launch_and_wait_for_gui_pane(self):
        self.harness.launch_gui_window(title="xwmux-focus-test")

        state = self.harness.wait_for_state(
            self.harness.state_has_gui_window,
            message="state dump never showed a GUI window",
        )

        gui_pane = self.harness.get_gui_panes(state)[0]

        self.harness.wait_for_state(
            lambda s: s.get("active")
            == {"window": gui_pane["window"], "pane": gui_pane["pane"]},
            message="GUI window wasn't focused after launching",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message="GUI window was not X-focused after launching",
        )

        return gui_pane
    @classmethod
    def setUpClass(cls):
        try:
            XwmuxTestHarness.check_prerequisites()
        except RuntimeError as err:
            raise unittest.SkipTest(str(err))

    def setUp(self):
        self.harness = XwmuxTestHarness()
        self.harness.start()

    def tearDown(self):
        self.harness.stop()

    def test_focus_follows_gui_vs_terminal_pane(self):
        gui_pane = self._launch_and_wait_for_gui_pane()

        self.harness.tmux("split-window", "-h", "-d")
        panes = self.harness.wait_for(
            lambda: p if len(p := self.harness.list_panes()) >= 3 else None,
            message="tmux never showed three panes after splitting",
        )

        terminal_pane = next(
            pane
            for pane in panes
            if pane["window"] == gui_pane["window"] and pane["pane"] != gui_pane["pane"]
        )
        terminal_window_id = self.harness.root_terminal_window_id()

        self.harness.tmux("select-pane", "-t", f"%{terminal_pane['pane']}")
        self.harness.wait_for_state(
            lambda s: s.get("active")
            == {"window": terminal_pane["window"], "pane": terminal_pane["pane"]},
            message="terminal pane never became active",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == terminal_window_id,
            message="focusing terminal pane did not focus the terminal window",
        )

        self.harness.tmux("select-pane", "-t", f"%{gui_pane['pane']}")
        self.harness.wait_for_state(
            lambda s: s.get("active")
            == {"window": gui_pane["window"], "pane": gui_pane["pane"]},
            message="GUI pane never became active",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message="focusing GUI pane did not focus the GUI window",
        )

        other_window = self.harness.create_window()
        self.harness.select_window(other_window)
        self.harness.wait_for_state(
            lambda s: s.get("active", {}).get("window") == other_window,
            message="new tmux window never became active",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == terminal_window_id,
            message="switching to another tmux window did not focus the terminal",
        )

        self.harness.select_window(gui_pane["window"])
        self.harness.wait_for_state(
            lambda s: s.get("active")
            == {"window": gui_pane["window"], "pane": gui_pane["pane"]},
            message="switching back did not restore the GUI pane as active",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message="switching back to the GUI tmux window did not focus the GUI window",
        )

    def test_zoomed_gui_pane_retains_focus(self):
        gui_pane = self._launch_and_wait_for_gui_pane()

        self.harness.tmux("resize-pane", "-Z", "-t", f"%{gui_pane['pane']}")

        self.harness.wait_for_state(
            lambda s: s.get("active")
            == {"window": gui_pane["window"], "pane": gui_pane["pane"]},
            message="zooming the GUI pane changed the active pane",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message="zooming the GUI pane did not initially preserve GUI focus",
        )

        time.sleep(3)
        self.assertEqual(
            self.harness.focused_window_id(),
            gui_pane["x_window"],
            "zooming the GUI pane did not preserve GUI focus after hooks settled",
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(FocusIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
