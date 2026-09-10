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

    def _split_active_window_with_gui(self, *, message):
        terminal_window_id = self.harness.root_terminal_window_id()
        self.harness.tmux("split-window", "-h", "-d")
        self.harness.wait_for(
            lambda: p if len(p := self.harness.list_panes()) >= 3 else None,
            message=message,
        )
        return terminal_window_id

    def _send_prefix_and_wait_for_tmux(self, terminal_window_id, *, focus_message, prefix_message):
        self.harness.wait_until_wm_synced()
        self.harness.send_key("ctrl+b")
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == terminal_window_id,
            message=focus_message,
        )
        self.harness.wait_for(
            self.harness.tmux_client_prefix_active,
            message=prefix_message,
        )

    def _assert_gui_focus_restored_and_prefix_cleared(
        self,
        gui_pane,
        *,
        focus_message,
        prefix_clear_message,
    ):
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message=focus_message,
        )
        self.assertFalse(
            self.harness.tmux_client_prefix_active(),
            prefix_clear_message,
        )

    def _terminal_pane_in_same_window(self, gui_pane):
        panes = self.harness.list_panes()
        return next(
            pane
            for pane in panes
            if pane["window"] == gui_pane["window"] and pane["pane"] != gui_pane["pane"]
        )

    def _wait_for_active_terminal_pane(self, terminal_pane, *, message):
        return self.harness.wait_for_state(
            lambda s: s.get("active")
            if s.get("active")
            == {"window": terminal_pane["window"], "pane": terminal_pane["pane"]}
            else None,
            message=message,
        )

    def _wait_for_window_zoom_state(self, window_id, zoomed, *, message):
        expected = f"@{window_id} {1 if zoomed else 0}"
        return self.harness.wait_for(
            lambda: expected
            if expected
            in self.harness.tmux("list-windows", "-a", "-F", "#{window_id} #{window_zoomed_flag}")
            else None,
            message=message,
        )

    def _wait_for_gui_hidden(self, gui_pane, *, message):
        return self.harness.wait_for_state(
            lambda s: pane_state
            if (
                (pane_state := s.get("workspaces", {})
                    .get(str(gui_pane["window"]), {})
                    .get("panes", {})
                    .get(str(gui_pane["pane"])))
                and pane_state.get("hidden")
            )
            else None,
            message=message,
        )

    def _wait_for_gui_visible(self, gui_pane, *, message):
        return self.harness.wait_for_state(
            lambda s: pane_state
            if (
                (pane_state := s.get("workspaces", {})
                    .get(str(gui_pane["window"]), {})
                    .get("panes", {})
                    .get(str(gui_pane["pane"])))
                and not pane_state.get("hidden")
            )
            else None,
            message=message,
        )

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
        gui_pane, log_path = self._launch_key_logger_and_wait_for_gui_pane()
        terminal_window_id = self.harness.root_terminal_window_id()

        self._send_prefix_and_wait_for_tmux(
            terminal_window_id,
            focus_message="first prefix did not focus the terminal window",
            prefix_message="first prefix did not activate tmux prefix mode",
        )

        before = self.harness.read_key_log(log_path)
        self.harness.send_key("ctrl+b")

        self._assert_gui_focus_restored_and_prefix_cleared(
            gui_pane,
            focus_message="second prefix did not restore GUI focus",
            prefix_clear_message="second prefix did not clear tmux prefix mode",
        )
        data = self.harness.wait_for(
            lambda: data if len(data := self.harness.read_key_log(log_path)) > len(before) else None,
            message="second prefix was not forwarded to the GUI window",
        )
        forwarded = data[len(before):]
        self.assertIn(
            b"\x02",
            forwarded,
            f"expected forwarded prefix input to reach kitty key logger, saw: {forwarded!r}",
        )

    def test_prefix_z_unzooms_gui_pane_and_retains_focus(self):
        gui_pane = self.harness.launch_and_wait_for_gui_pane(title="xwmux-prefix-z-test")
        full_size = self.harness.window_size(gui_pane["x_window"])
        terminal_window_id = self._split_active_window_with_gui(
            message="tmux never showed three panes after splitting before prefix z",
        )
        split_size = self.harness.wait_for(
            lambda: size
            if (size := self.harness.window_size(gui_pane["x_window"])) != full_size
            else None,
            message="GUI window never settled to a split size before prefix z",
        )
        gui_pane = self.harness.wait_for_gui_pane(
            x_window=gui_pane["x_window"],
            message="GUI pane mapping changed before prefix z testing",
        )

        self._send_prefix_and_wait_for_tmux(
            terminal_window_id,
            focus_message="prefix before first z did not focus the terminal window",
            prefix_message="prefix before first z did not activate tmux prefix mode",
        )
        self.harness.send_key("z")

        self._wait_for_window_zoom_state(
            gui_pane["window"],
            True,
            message="first prefix z did not leave the tmux window zoomed",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message="first prefix z did not restore GUI focus to the zoomed pane",
        )
        self.assertFalse(
            self.harness.tmux_client_prefix_active(),
            "first prefix z did not clear tmux prefix mode",
        )

        self._send_prefix_and_wait_for_tmux(
            terminal_window_id,
            focus_message="prefix before second z did not focus the terminal window",
            prefix_message="prefix before second z did not activate tmux prefix mode",
        )
        self.harness.send_key("z")

        self.harness.wait_for(
            lambda: (
                active if (active := self.harness.active_gui_pane(self.harness.dump_state()))
                and active["x_window"] == gui_pane["x_window"]
                else None
            ),
            message="second prefix z changed the active pane",
        )
        self._wait_for_window_zoom_state(
            gui_pane["window"],
            False,
            message="second prefix z did not leave the tmux window unzoomed",
        )
        self.harness.wait_for(
            lambda: self.harness.window_size(gui_pane["x_window"]) == split_size,
            message=(
                "second prefix z did not unzoom the GUI window back to its split size "
                f"(full_size={full_size}, split_size={split_size}, "
                f"cur_size={self.harness.window_size(gui_pane['x_window'])})"
            ),
        )
        self._assert_gui_focus_restored_and_prefix_cleared(
            gui_pane,
            focus_message="second prefix z did not preserve GUI focus after unzooming",
            prefix_clear_message="second prefix z did not clear tmux prefix mode",
        )

    def test_selecting_other_pane_after_zooming_gui_unzooms_and_focuses_terminal(self):
        gui_pane = self.harness.launch_and_wait_for_gui_pane(title="xwmux-zoom-select-test")
        full_size = self.harness.window_size(gui_pane["x_window"])
        terminal_window_id = self._split_active_window_with_gui(
            message="tmux never showed three panes after splitting before selecting another pane",
        )
        split_size = self.harness.wait_for(
            lambda: size
            if (size := self.harness.window_size(gui_pane["x_window"])) != full_size
            else None,
            message="GUI window never settled to a split size before zooming",
        )
        gui_pane = self.harness.wait_for_gui_pane(
            x_window=gui_pane["x_window"],
            message="GUI pane mapping changed before zoom/select testing",
        )
        terminal_pane = self._terminal_pane_in_same_window(gui_pane)

        self.harness.tmux("resize-pane", "-Z", "-t", f"%{gui_pane['pane']}")
        self.harness.wait_for(
            lambda: self.harness.window_size(gui_pane["x_window"]) == full_size,
            message="zooming the GUI pane did not make it full-size",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == gui_pane["x_window"],
            message="zooming the GUI pane did not preserve GUI focus",
        )

        self.harness.tmux("select-pane", "-t", f"%{terminal_pane['pane']}")

        self._wait_for_active_terminal_pane(
            terminal_pane,
            message="selecting the terminal pane did not make it active",
        )
        self.harness.wait_for(
            lambda: self.harness.window_size(gui_pane["x_window"]) == split_size,
            message=(
                "selecting another pane did not unzoom the GUI window back to its split size "
                f"(full_size={full_size}, split_size={split_size}, "
                f"cur_size={self.harness.window_size(gui_pane['x_window'])})"
            ),
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == terminal_window_id,
            message="selecting another pane after zooming a GUI pane did not focus the terminal window",
        )

    def test_zooming_terminal_hides_gui_until_unzoom(self):
        gui_pane = self.harness.launch_and_wait_for_gui_pane(title="xwmux-terminal-zoom-test")
        terminal_window_id = self._split_active_window_with_gui(
            message="tmux never showed three panes after splitting before terminal zoom",
        )
        gui_pane = self.harness.wait_for_gui_pane(
            x_window=gui_pane["x_window"],
            message="GUI pane mapping changed before terminal zoom testing",
        )
        terminal_pane = self._terminal_pane_in_same_window(gui_pane)

        self._wait_for_gui_visible(
            gui_pane,
            message="GUI pane was not visible before zooming the terminal pane",
        )

        self.harness.tmux("select-pane", "-t", f"%{terminal_pane['pane']}")
        self._wait_for_active_terminal_pane(
            terminal_pane,
            message="terminal pane never became active before zooming",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == terminal_window_id,
            message="selecting the terminal pane did not focus the terminal window before zooming",
        )

        self.harness.tmux("resize-pane", "-Z", "-t", f"%{terminal_pane['pane']}")
        self._wait_for_gui_hidden(
            gui_pane,
            message="zooming the terminal pane did not hide the GUI pane",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == terminal_window_id,
            message="zooming the terminal pane did not keep the terminal window focused",
        )

        self.harness.tmux("resize-pane", "-Z", "-t", f"%{terminal_pane['pane']}")
        self._wait_for_gui_visible(
            gui_pane,
            message="unzooming the terminal pane did not make the GUI pane visible again",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == terminal_window_id,
            message="unzooming the terminal pane did not keep the terminal window focused",
        )

    def test_prefix_bang_breaks_out_active_gui_pane_and_retains_focus(self):
        gui_pane = self.harness.launch_and_wait_for_gui_pane(title="xwmux-prefix-bang-test")
        terminal_window_id = self._split_active_window_with_gui(
            message="tmux never showed three panes after splitting before prefix bang",
        )

        self._send_prefix_and_wait_for_tmux(
            terminal_window_id,
            focus_message="prefix before bang did not focus the terminal window",
            prefix_message="prefix before bang did not activate tmux prefix mode",
        )

        self.harness.send_key("shift+1")

        self.harness.wait_for_state(
            lambda s: s.get("active")
            if s.get("active", {}).get("pane") == gui_pane["pane"]
            and s.get("active", {}).get("window") != gui_pane["window"]
            else None,
            message="prefix ! did not move the active GUI pane into a new tmux window",
        )

        self._assert_gui_focus_restored_and_prefix_cleared(
            gui_pane,
            focus_message="prefix ! did not preserve GUI focus",
            prefix_clear_message="prefix ! did not clear tmux prefix mode",
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PrefixIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
