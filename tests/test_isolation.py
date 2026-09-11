#!/usr/bin/env python3

"""Regression coverage for the integration launcher's isolation boundary."""

import os
import sys
import unittest
from pathlib import Path

from xwmux_test_harness import XwmuxTestHarness


class IsolationIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            XwmuxTestHarness.check_prerequisites()
        except RuntimeError as err:
            raise unittest.SkipTest(str(err))

    def test_launcher_ignores_callers_display_and_starts_xvfb(self):
        # A valid desktop DISPLAY here must have exactly the same result.  A
        # deliberately bogus one makes a regression to the old behavior fail
        # deterministically instead of opening a real Xephyr window.
        old_display = os.environ.get("DISPLAY")
        os.environ["DISPLAY"] = ":424242"
        harness = XwmuxTestHarness()
        try:
            startup = harness.start()
            self.assertEqual(startup["STARTED_XVFB"], "1")
            self.assertNotEqual(startup["HOST_DISPLAY"], ":424242")
            self.assertNotEqual(startup["DISPLAY"], startup["HOST_DISPLAY"])
            harness.wait_until_ready()
            runtime_dir = Path(startup["TMUX_SOCKET"]).parent
            harness.stop()
            self.assertFalse(runtime_dir.exists(), "test runtime directory was not cleaned up")
        finally:
            harness.stop()
            if old_display is None:
                os.environ.pop("DISPLAY", None)
            else:
                os.environ["DISPLAY"] = old_display


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(IsolationIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
