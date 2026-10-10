"""PyInstaller entry point (absolute imports so the frozen app behaves like `python -m wattpulse`)."""
import sys

from wattpulse.app import main

if __name__ == "__main__":
    sys.exit(main())
