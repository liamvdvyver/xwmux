#!/usr/bin/env python3

import sys
import unittest

from xwmux_test_harness import XwmuxTestHarness


class DumpStateIntegrationTest(unittest.TestCase):
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

    def test_dump_state_includes_gui_window(self):
        self.harness.launch_gui_window()

        state = self.harness.wait_for_state(
            self.harness.state_has_gui_window,
            message="state dump never showed a GUI window",
        )

        self.assertTrue(self.harness.state_has_gui_window(state))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(DumpStateIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
