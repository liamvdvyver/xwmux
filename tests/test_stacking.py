#!/usr/bin/env python3

import sys
import unittest

from xwmux_test_harness import XwmuxTestHarness


class StackingIntegrationTest(unittest.TestCase):
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

    def test_reopened_root_terminal_stays_below_existing_gui_window(self):
        gui_pane = self.harness.launch_and_wait_for_gui_pane(
            title="xwmux-stacking-test"
        )
        term_window = self.harness.root_terminal_window_id()

        self.harness.kill_window(term_window)

        reopened_term = self.harness.wait_for(
            lambda: self.harness.focused_window_id()
            if self.harness.focused_window_id() == self.harness.root_terminal_window_id()
            else None,
            message="root terminal did not regain focus after being killed",
        )

        self.harness.wait_for(
            lambda: self.harness.is_below(reopened_term, gui_pane["x_window"]),
            message=(
                f"reopened root terminal {reopened_term} did not stay below the existing GUI window {gui_pane['x_window']} "
                f"(stack={self.harness.root_child_stack()})"
            ),
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(StackingIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
