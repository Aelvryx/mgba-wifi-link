#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Pad a test cartridge image and patch its GBA header complement byte."""

from pathlib import Path
import sys

MINIMUM_CARTRIDGE_SIZE = 32 * 1024


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fix-header.py ROM", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    rom = bytearray(path.read_bytes())
    if len(rom) < 0xC0:
        print("ROM is shorter than the GBA header", file=sys.stderr)
        return 1
    if len(rom) < MINIMUM_CARTRIDGE_SIZE:
        rom.extend(b"\xFF" * (MINIMUM_CARTRIDGE_SIZE - len(rom)))
    checksum = (-sum(rom[0xA0:0xBD]) - 0x19) & 0xFF
    rom[0xBD] = checksum
    path.write_bytes(rom)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
