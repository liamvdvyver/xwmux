#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xwmux_test_harness import XwmuxTestHarness


class InitHooksIntegrationTest(unittest.TestCase):
    def setUp(self):
        self.harness = XwmuxTestHarness()
        self.harness.start()
        self.harness.wait_until_ready()

    def tearDown(self):
        self.harness.stop()

    def test_initializer_installs_direct_refresh_hooks(self):
        hooks = self.harness.tmux("show-hooks", "-g")

        for hook in [
            "after-split-window",
            "window-linked",
            "window-unlinked",
            "after-kill-pane",
            "after-new-window",
            "after-select-window",
            "client-attached",
        ]:
            self.assertIn(f"{hook}[69]", hooks)

        self.assertIn('after-split-window[69] run-shell "xwmux-ctl refresh"', hooks)
        self.assertIn(
            'after-kill-pane[69] run-shell "xwmux-ctl kill-pane orphans && xwmux-ctl refresh"',
            hooks,
        )
        self.assertIn(
            'window-layout-changed[69] run-shell "xwmux-ctl refresh"',
            self.harness.tmux("show-hooks", "-g", "window-layout-changed"),
        )
        self.assertNotIn("xwmux-report.sh", hooks)
        self.assertEqual(self.harness.tmux("show-options", "-g", "focus-events").strip(), "focus-events on")


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(InitHooksIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
