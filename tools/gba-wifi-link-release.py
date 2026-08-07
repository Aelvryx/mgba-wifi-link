#!/usr/bin/env python3
"""Run the non-publishing GBA Wi-Fi Link release tool."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.gba_wifi_link_release.cli import main


if __name__ == "__main__":
    raise SystemExit(main())
