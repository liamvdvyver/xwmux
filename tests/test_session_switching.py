#!/usr/bin/env python3

import sys
import unittest

from xwmux_test_harness import XwmuxTestHarness


class SessionSwitchingIntegrationTest(unittest.TestCase):
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

    def _search_window_by_title(self, title):
        output = self.harness._safe_command_output(["xdotool", "search", "--name", title])
        if output.startswith("<exit "):
            return None
        lines = [line.strip() for line in output.splitlines() if line.strip()]
        if not lines:
            return None
        return int(lines[-1])

    def _wait_for_gui_pane_for_title(self, title):
        x_window = self.harness.wait_for(
            lambda: self._search_window_by_title(title),
            message=f"window with title {title!r} did not appear",
        )
        state = self.harness.wait_for_state(
            lambda s: next(
                (
                    {
                        "window": int(tm_window),
                        "pane": int(tm_pane),
                        "x_window": x_window,
                    }
                    for tm_window, workspace in s.get("workspaces", {}).items()
                    for tm_pane, pane_state in workspace.get("panes", {}).items()
                    if int(pane_state["window"]) == x_window
                ),
                None,
            ),
            message=f"state never mapped GUI window titled {title!r}",
        )
        return next(
            {
                "window": int(tm_window),
                "pane": int(tm_pane),
                "x_window": x_window,
            }
            for tm_window, workspace in state.get("workspaces", {}).items()
            for tm_pane, pane_state in workspace.get("panes", {}).items()
            if int(pane_state["window"]) == x_window
        )

    def _launch_gui_pane_for_title(self, title):
        self.harness.launch_gui_window(title=title)
        return self._wait_for_gui_pane_for_title(title)

    def _wait_for_gui_hidden_state(self, gui_pane, hidden, *, message):
        x_window = gui_pane["x_window"]
        return self.harness.wait_for_state(
            lambda s: pane_state
            if (
                (pane_state := next(
                    (
                        pane_state
                        for workspace in s.get("workspaces", {}).values()
                        for pane_state in workspace.get("panes", {}).values()
                        if int(pane_state["window"]) == x_window
                    ),
                    None,
                ))
                and pane_state.get("hidden") == hidden
            )
            else None,
            message=message,
        )

    def _current_session_name(self):
        return self.harness.tmux("display-message", "-p", "#{session_name}").strip()

    def _attached_client_session_name(self):
        output = self.harness.tmux("list-clients", "-F", "#{session_name}")
        lines = [line.strip() for line in output.splitlines() if line.strip()]
        if not lines:
            return None
        return lines[-1]

    def _switch_session(self, name):
        self.harness.tmux("switch-client", "-t", name)

    def _create_session(self, name):
        self.harness.tmux("new-session", "-d", "-s", name)

    def _wait_for_active_window(self, window_id, *, message):
        return self.harness.wait_for_state(
            lambda s: s.get("active")
            if s.get("active", {}).get("window") == window_id
            else None,
            message=message,
        )

    def _select_window_and_wait(self, window_id, *, x_window=None, message):
        def select_and_check():
            self.harness.select_window(window_id)
            state = self.harness.dump_state()
            if state.get("active", {}).get("window") != window_id:
                return None
            if x_window is not None and self.harness.focused_window_id() != x_window:
                return None
            return state

        return self.harness.wait_for(
            select_and_check,
            message=message,
        )

    def test_switching_between_tmux_windows_hides_old_gui_and_shows_new_gui(self):
        window1_gui = self._launch_gui_pane_for_title("xwmux-window-switch-1")
        self._wait_for_gui_hidden_state(
            window1_gui,
            False,
            message="launching the first GUI window did not show it",
        )

        second_window = self.harness.create_window()
        self._select_window_and_wait(
            second_window,
            message="selecting the second tmux window did not make it active before launching its GUI",
        )
        window2_gui = self._launch_gui_pane_for_title("xwmux-window-switch-2")
        self.assertNotEqual(window2_gui["window"], window1_gui["window"])

        self._wait_for_gui_hidden_state(
            window1_gui,
            True,
            message="switching to the second tmux window did not hide the first GUI window",
        )
        self._wait_for_gui_hidden_state(
            window2_gui,
            False,
            message="switching to the second tmux window did not show the second GUI window",
        )



    def test_switching_sessions_hides_old_gui_and_restores_non_default_session_after_detach(self):
        default_gui = self.harness.launch_and_wait_for_gui_pane(
            title="xwmux-default-session-gui"
        )
        self.assertEqual(self._current_session_name(), "default")

        self._create_session("alt")
        self._switch_session("alt")
        self.harness.wait_for(
            lambda: self._current_session_name() == "alt",
            message="switch-client did not move the client to the alt session",
        )

        self._wait_for_gui_hidden_state(
            default_gui,
            True,
            message="switching to the alt session did not hide the default session GUI",
        )

        alt_gui = self._launch_gui_pane_for_title("xwmux-alt-session-gui")

        self._wait_for_gui_hidden_state(
            alt_gui,
            False,
            message="the alt session GUI was not shown after launching it",
        )
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == alt_gui["x_window"],
            message="the alt session GUI did not receive X focus",
        )

        self.harness.tmux("detach-client")

        self.harness.wait_for(
            lambda: self.harness.root_terminal_window_id(),
            message="root terminal was not restored after detaching from tmux",
        )
        self.harness.wait_for(
            lambda: self._attached_client_session_name() == "alt",
            message="reattaching after detach did not restore the attached client to the previously active non-default session",
        )
        self._wait_for_gui_hidden_state(
            default_gui,
            True,
            message="reattaching after detach unexpectedly showed the default session GUI",
        )
        self._wait_for_gui_hidden_state(
            alt_gui,
            False,
            message="reattaching after detach did not show the alt session GUI",
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SessionSwitchingIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
