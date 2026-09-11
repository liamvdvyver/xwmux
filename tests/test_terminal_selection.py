#!/usr/bin/env python3

import os
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "src" / "scripts" / "xwmux-launch-term.sh"


class TerminalSelectionTest(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.bin_dir = Path(self.tmpdir.name) / "bin"
        self.bin_dir.mkdir()
        self.output = Path(self.tmpdir.name) / "terminal"
        self.write_program("tmux", """#!/usr/bin/env sh
case "$1" in
    start-server) exit 0 ;;
    show-options) printf '%s\\n' "${TMUX_OPTION:-}" ;;
esac
""")
        for terminal in ("kitty", "xterm"):
            self.write_program(terminal, """#!/usr/bin/env sh
printf '%s\\n' "$(basename "$0")" > "$XWMUX_TEST_OUTPUT"
""")

    def tearDown(self):
        self.tmpdir.cleanup()

    def write_program(self, name, content):
        path = self.bin_dir / name
        path.write_text(content)
        path.chmod(0o755)

    def run_launcher(self, *, tmux_option="", terminal=""):
        env = os.environ.copy()
        env.update({
            "PATH": f"{self.bin_dir}:{env['PATH']}",
            "TMUX_OPTION": tmux_option,
            "TERMINAL": terminal,
            "XWMUX_TEST_OUTPUT": str(self.output),
        })
        return subprocess.run([str(LAUNCHER)], env=env, text=True,
                              capture_output=True, check=False)

    def selected_terminal(self):
        for _ in range(20):
            if self.output.exists():
                return self.output.read_text().strip()
            time.sleep(0.01)
        self.fail("terminal launcher did not run")

    def test_tmux_option_takes_precedence_over_terminal_environment(self):
        proc = self.run_launcher(tmux_option="kitty", terminal="xterm")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(self.selected_terminal(), "kitty")

    def test_matching_terminal_environment_is_fallback(self):
        proc = self.run_launcher(terminal="xterm")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(self.selected_terminal(), "xterm")

    def test_unsupported_terminal_environment_is_ignored(self):
        proc = self.run_launcher(terminal="unsupported")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(self.selected_terminal(), "kitty")

    def test_invalid_tmux_option_fails_without_fallback(self):
        proc = self.run_launcher(tmux_option="unsupported", terminal="xterm")
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("unsupported @xwmux-terminal value", proc.stderr)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
