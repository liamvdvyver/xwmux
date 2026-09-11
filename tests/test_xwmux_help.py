#!/usr/bin/env python3

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
XWMUX = ROOT / "build" / "xwmux"
USAGE = "Usage: xwmux [--tmux-socket <path>] [-v|-vv|-vvv]"


class XwmuxHelpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not XWMUX.exists():
            raise unittest.SkipTest(f"missing binary: {XWMUX}")

    def run_xwmux(self, *args):
        return subprocess.run(
            [str(XWMUX), *args], text=True, capture_output=True, check=False
        )

    def test_help_succeeds(self):
        proc = self.run_xwmux("--help")
        self.assertEqual(proc.returncode, 0)
        self.assertEqual(proc.stdout.strip(), USAGE)
        self.assertEqual(proc.stderr, "")

    def test_invalid_arguments_fail_with_usage(self):
        for args in [
            ("--tmux-socket",),
            ("--tmux-socket", "one", "--tmux-socket", "two"),
            ("--unknown",),
        ]:
            with self.subTest(args=args):
                proc = self.run_xwmux(*args)
                self.assertNotEqual(proc.returncode, 0)
                self.assertEqual(proc.stdout, "")
                self.assertEqual(proc.stderr.strip(), USAGE)


if __name__ == "__main__":
    unittest.main()
