#!/usr/bin/env python3

import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
XWMUX_CTL = ROOT / "build" / "xwmux-ctl"


class XwmuxCtlHelpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not XWMUX_CTL.exists():
            raise unittest.SkipTest(f"missing binary: {XWMUX_CTL}")

    def run_ctl(self, *args):
        return subprocess.run(
            [str(XWMUX_CTL), *args],
            text=True,
            capture_output=True,
            check=False,
        )

    def test_general_help_lists_supported_commands(self):
        proc = self.run_ctl("--help")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("Usage: xwmux-ctl <command> [args]", proc.stdout)
        self.assertIn("xwmux-ctl exit", proc.stdout)
        self.assertIn("xwmux-ctl dump-state", proc.stdout)
        self.assertIn("xwmux-ctl kill-pane", proc.stdout)

    def test_command_help_prints_usage_only(self):
        proc = self.run_ctl("kill-pane", "--help")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout.strip(), "Usage: xwmux-ctl kill-pane [ %<pane-id> | focused | orphans ]")


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(XwmuxCtlHelpTest)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
