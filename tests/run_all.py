#!/usr/bin/env python3

import pathlib
import sys
import unittest


def main():
    tests_dir = pathlib.Path(__file__).resolve().parent
    loader = unittest.defaultTestLoader
    suite = loader.discover(str(tests_dir), pattern="test_*.py")
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
