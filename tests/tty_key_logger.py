#!/usr/bin/env python3

import os
import sys
import termios
import tty
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <log-path>", file=sys.stderr)
        return 1

    log_path = Path(sys.argv[1])
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text("")

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        while True:
            data = os.read(fd, 32)
            if not data:
                return 0
            with log_path.open("ab") as f:
                f.write(data)
                f.flush()
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


if __name__ == "__main__":
    raise SystemExit(main())
