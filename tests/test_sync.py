#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xwmux_test_harness import XwmuxTestHarness


class SyncIntegrationTest(unittest.TestCase):
    def setUp(self):
        self.harness = XwmuxTestHarness()
        self.harness.start()
        self.harness.wait_until_ready()

    def tearDown(self):
        self.harness.stop()

    def test_sync_acknowledges_refresh_after_tmux_window_switch(self):
        gui_pane = self.harness.launch_and_wait_for_gui_pane(title="xwmux-sync-test")
        terminal_window = self.harness.create_window()

        self.harness.tmux_and_wait("select-window", "-t", f"@{terminal_window}")

        state = self.harness.dump_state()
        self.assertEqual(state["active"]["window"], terminal_window)
        self.assertEqual(
            self.harness.focused_window_id(),
            self.harness.root_terminal_window_id(),
        )
        self.assertNotEqual(gui_pane["window"], terminal_window)

    def test_sync_preserves_active_prefix_override(self):
        gui_pane = self.harness.launch_and_wait_for_gui_pane(
            title="xwmux-sync-prefix-override-test"
        )
        root_terminal = self.harness.root_terminal_window_id()

        self.harness.send_key("ctrl+b")
        self.harness.wait_for(
            lambda: self.harness.focused_window_id() == root_terminal,
            message="prefix did not focus the root terminal",
        )
        self.harness.wait_for(
            self.harness.tmux_client_prefix_active,
            message="prefix did not activate tmux prefix mode",
        )

        self.harness.wait_until_wm_synced()

        state = self.harness.dump_state()
        self.assertTrue(state["overridden"])
        self.assertTrue(self.harness.tmux_client_prefix_active())
        self.assertEqual(self.harness.focused_window_id(), root_terminal)
        self.assertEqual(
            self.harness.active_gui_pane(state)["x_window"], gui_pane["x_window"]
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SyncIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
