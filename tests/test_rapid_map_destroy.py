#!/usr/bin/env python3

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from xwmux_test_harness import XwmuxTestHarness


ROOT = Path(__file__).resolve().parents[1]
RAPID_MAP_DESTROY_SOURCE = ROOT / "tests" / "rapid_map_destroy.c"


class RapidMapDestroyIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            XwmuxTestHarness.check_prerequisites()
            XwmuxTestHarness.require_program("cc")
        except RuntimeError as err:
            raise unittest.SkipTest(str(err))

    def setUp(self):
        self.harness = XwmuxTestHarness()
        self.harness.start()
        self.harness.wait_until_ready()
        self.temp_dir = tempfile.TemporaryDirectory(prefix="xwmux-rapid-map-destroy.")
        self.client = Path(self.temp_dir.name) / "rapid-map-destroy"
        subprocess.run(
            ["cc", str(RAPID_MAP_DESTROY_SOURCE), "-lX11", "-o", str(self.client)],
            check=True,
        )

    def tearDown(self):
        self.temp_dir.cleanup()
        self.harness.stop()

    def _assert_destroyed_window_does_not_produce_x_errors(self, *args):
        subprocess.run([str(self.client), *args], env=self.harness.test_env, check=True)
        self.harness.wait_until_wm_synced()

        x_errors = [
            line for line in self.harness.launcher_logs if "X11 error:" in line
        ]
        self.assertEqual(x_errors, [], "a destroyed window produced X11 errors")

    def test_destroyed_map_request_does_not_produce_x_errors(self):
        self._assert_destroyed_window_does_not_produce_x_errors()

    def test_destroyed_mapped_window_does_not_produce_x_errors(self):
        self._assert_destroyed_window_does_not_produce_x_errors("100000")


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(RapidMapDestroyIntegrationTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
