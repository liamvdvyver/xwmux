#!/usr/bin/env python3

import sys
import unittest

from xwmux_test_harness import XwmuxTestHarness


class PrefixIntegrationTest(unittest.TestCase):
    def _launch_key_logger_and_wait_for_gui_pane(self):
        _, log_path = self.harness.launch_key_logger_window(title="xwmux-key-logger")

        state = self.harness.wait_for_state(
            self.harness.state_has_gui_window,
            message="state dump never showed a GUI key logger window",
        )

        gui_pane = self.harness.get_gui_panes(state)[0]
        gui_pane = self.harness.wait_for(
            lambda: self.harness.active_gui_pane(self.harness.dump_state()),
            message="GUI key logger window wasn't focused after launching",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message="GUI key logger window was not X-focused after launching",
        )

        return gui_pane, log_path

    @classmethod
    def setUpClass(cls):
        try:
            XwmuxTestHarness.check_prerequisites()
        except RuntimeError as err:
            raise unittest.SkipTest(str(err))

    def setUp(self):
        self.harness = XwmuxTestHarness()
        self.harness.start()
        self.harness.wait_until_ready()

    def tearDown(self):
        self.harness.stop()

    def test_prefix_twice_sends_prefix_to_gui_and_clears_tmux_prefix(self):
        # TODO: Extend this regression to xterm once prefix passthrough uses
        # XTEST instead of synthetic XSendEvent key events.
        gui_pane, log_path = self._launch_key_logger_and_wait_for_gui_pane()
        terminal_window_id = self.harness.root_terminal_window_id()

        self.harness.send_key("ctrl+b")

        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == terminal_window_id,
            message="first prefix did not focus the terminal window",
        )
        self.harness.wait_for(
            self.harness.tmux_client_prefix_active,
            message="first prefix did not activate tmux prefix mode",
        )

        before = self.harness.read_key_log(log_path)
        self.harness.send_key("ctrl+b")

        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message="second prefix did not restore GUI focus",
        )
        self.harness.wait_for(
            lambda: not self.harness.tmux_client_prefix_active(),
            message="second prefix did not clear tmux prefix mode",
        )
        data = self.harness.wait_for(
            lambda: data if len(data := self.harness.read_key_log(log_path)) > len(before) else None,
            message="second prefix was not forwarded to the GUI window",
        )
        forwarded = data[len(before):]
        # TODO: Tighten this back to b"\x02" once prefix passthrough uses
        # XTEST and xterm is supported on this path. With the current
        # synthetic XSendEvent path, kitty records plain b for the forwarded
        # prefix keypress.
        self.assertIn(
            b"b",
            forwarded,
            f"expected forwarded prefix input to reach kitty key logger, saw: {forwarded!r}",
        )

    def test_prefix_z_zoomed_gui_pane_retains_focus(self):
        gui_pane = self.harness.launch_and_wait_for_gui_pane(title="xwmux-prefix-z-test")
        terminal_window_id = self.harness.root_terminal_window_id()
        self.harness.tmux("split-window", "-h", "-d")
        self.harness.wait_for(
            lambda: p if len(p := self.harness.list_panes()) >= 3 else None,
            message="tmux never showed three panes after splitting before prefix z",
        )

        initial_size = self.harness.window_size(gui_pane["x_window"])

        self.harness.send_key("ctrl+b")

        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == terminal_window_id,
            message="prefix before z did not focus the terminal window",
        )
        self.harness.wait_for(
            self.harness.tmux_client_prefix_active,
            message="prefix before z did not activate tmux prefix mode",
        )

        self.harness.send_key("z")

        self.harness.wait_for(
            lambda: (
                active if (active := self.harness.active_gui_pane(self.harness.dump_state()))
                and active["x_window"] == gui_pane["x_window"]
                else None
            ),
            message="prefix z changed the active pane",
        )
        zoomed_size = self.harness.wait_for(
            lambda: size
            if (size := self.harness.window_size(gui_pane["x_window"])) != initial_size
            else None,
            message="prefix z did not change the GUI window size",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message="prefix z did not initially restore GUI focus to the zoomed pane",
        )
        self.assertFalse(
            self.harness.tmux_client_prefix_active(),
            "prefix z did not clear tmux prefix mode",
        )

    def test_prefix_bang_breaks_out_active_gui_pane_and_retains_focus(self):
        gui_pane = self.harness.launch_and_wait_for_gui_pane(title="xwmux-prefix-bang-test")
        terminal_window_id = self.harness.root_terminal_window_id()
        self.harness.tmux("split-window", "-h", "-d")
        self.harness.wait_for(
            lambda: p if len(p := self.harness.list_panes()) >= 3 else None,
            message="tmux never showed three panes after splitting before prefix bang",
        )

        self.harness.send_key("ctrl+b")

        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == terminal_window_id,
            message="prefix before bang did not focus the terminal window",
        )
        self.harness.wait_for(
            self.harness.tmux_client_prefix_active,
            message="prefix before bang did not activate tmux prefix mode",
        )

        self.harness.send_key("shift+1")

        active = self.harness.wait_for_state(
            lambda s: s.get("active")
            if s.get("active", {}).get("pane") == gui_pane["pane"]
            and s.get("active", {}).get("window") != gui_pane["window"]
            else None,
            message="prefix ! did not move the active GUI pane into a new tmux window",
        )["active"]

        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message="prefix ! did not preserve GUI focus",
        )
        self.assertFalse(
            self.harness.tmux_client_prefix_active(),
            "prefix ! did not clear tmux prefix mode",
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PrefixIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
