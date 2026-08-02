#!/usr/bin/env python3
"""Fail closed when a supported protocol-v1 surface reappears."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

RETIRED_PATHS = (
    "include/mgba/internal/gba/sio/netplay/driver.h",
    "include/mgba/internal/gba/sio/netplay/identity-v1.h",
    "include/mgba/internal/gba/sio/netplay/protocol.h",
    "include/mgba/internal/gba/sio/netplay/session.h",
    "include/mgba/internal/gba/sio/netplay/timeline.h",
    "src/gba/sio/netplay/driver.c",
    "src/gba/sio/netplay/protocol.c",
    "src/gba/sio/netplay/session.c",
    "src/gba/sio/netplay/timeline.c",
    "src/platform/libretro/netpacket.c",
    "src/platform/libretro/netpacket.h",
    "src/gba/test/netplay-driver.c",
    "src/gba/test/netplay-integration.c",
    "src/gba/test/netplay-protocol.c",
    "src/gba/test/netplay-session.c",
    "src/platform/test/libretro-netpacket.c",
    "tools/analyze-link-netplay-log.py",
    "tools/test-analyze-link-netplay-log.py",
)

RETIRED_TARGETS = (
    "test-gba-netplay-driver",
    "test-gba-netplay-integration",
    "test-gba-netplay-protocol",
    "test-gba-netplay-session",
    "test-libretro-netpacket",
)

ACTIVE_ROOTS = (
    "include",
    "src",
    "tools",
    ".github",
)

CURRENT_INSTRUCTIONS = (
    "README.md",
    "ROADMAP.md",
    "UPSTREAM.md",
    "docs/wifi-link-netplay.md",
)

FORBIDDEN_ACTIVE_TEXT = (
    "mgba_gba_link_netplay_runtime",
    "cable-v1",
    "netplayV1Diagnostic",
    "mgba-gba-link-netplay-v1",
    "GBASIONetplayDriver",
    "mLibretroNetpacketRegister",
    "analyze-link-netplay-log.py",
)

TEXT_SUFFIXES = {
    ".c", ".h", ".cmake", ".cfg", ".json", ".md", ".opt",
    ".py", ".sh", ".txt", ".yaml", ".yml",
}


def fail(message: str) -> None:
    print(f"protocol-v1 absence audit: {message}", file=sys.stderr)
    raise SystemExit(1)


def active_files() -> list[Path]:
    files: list[Path] = []
    self_path = Path(__file__).resolve()
    raw_fixture = (ROOT / "src/gba/test/netplay-legacy-wire-fixture.h").resolve()
    for relative in ACTIVE_ROOTS:
        root = ROOT / relative
        for path in root.rglob("*"):
            if path.resolve() in (self_path, raw_fixture) or not path.is_file():
                continue
            if "openspec/changes/archive" in path.as_posix():
                continue
            if path.suffix.lower() in TEXT_SUFFIXES or path.name == "CMakeLists.txt":
                files.append(path)
    return files


def audit_source() -> None:
    for relative in RETIRED_PATHS:
        if (ROOT / relative).exists():
            fail(f"retired path still exists: {relative}")

    libretro = (ROOT / "src/platform/libretro/libretro.c").read_text(
        encoding="utf-8"
    )
    options = (ROOT / "src/platform/libretro/libretro_core_options.h").read_text(
        encoding="utf-8"
    )
    for stale_value in ("cable-v1", "replicated-v2", "arbitrary-retired-value"):
        if stale_value in libretro or stale_value in options:
            fail(f"frontend still acts on retired selector value: {stale_value}")
    if "mgba_gba_link_netplay_runtime" in libretro + options:
        fail("frontend still declares or queries the retired runtime selector")
    if "mLibretroNetpacketV2Register(" not in libretro:
        fail("libretro no longer registers protocol v2 directly")

    instruction_text = {
        relative: (ROOT / relative).read_text(encoding="utf-8")
        for relative in CURRENT_INSTRUCTIONS
    }
    for relative, text in instruction_text.items():
        for forbidden in ('"cable-v1"', "Cable Sync v1"):
            if forbidden in text:
                fail(f"current instructions still select v1 in {relative}")
    selector_mentions = [
        relative
        for relative, text in instruction_text.items()
        if "mgba_gba_link_netplay_runtime" in text
    ]
    if selector_mentions != ["docs/wifi-link-netplay.md"]:
        fail("retired selector is not confined to the Wi-Fi guide migration note")
    wifi_guide = instruction_text["docs/wifi-link-netplay.md"]
    if not ({"ignored", "inert"} & set(wifi_guide.split())) or \
            "only shipped" not in wifi_guide:
        fail("Wi-Fi guide does not describe the old selector as inert")
    if "Remove the obsolete distributed-cable v1 runtime" in instruction_text["README.md"]:
        fail("root README still presents protocol-v1 removal as future work")

    for path in active_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        for forbidden in FORBIDDEN_ACTIVE_TEXT:
            if forbidden in text:
                fail(f"active reference {forbidden!r} in {path.relative_to(ROOT)}")
        for target in RETIRED_TARGETS:
            if re.search(rf"(?<![A-Za-z0-9_-]){re.escape(target)}(?![A-Za-z0-9_-])", text):
                fail(f"retired target {target!r} in {path.relative_to(ROOT)}")


def audit_generated_targets(build_dir: Path) -> None:
    result = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", "help"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.returncode:
        fail(f"cannot inspect generated targets in {build_dir}: {result.stdout}")
    for target in RETIRED_TARGETS:
        if re.search(rf"(?m)^\s*{re.escape(target)}(?:\s|:|$)", result.stdout):
            fail(f"generated target still exists: {target}")


def tool_output(command: list[str]) -> str:
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.returncode:
        fail(f"command failed ({' '.join(command)}): {result.stdout}")
    return result.stdout


def audit_binary(binary: Path) -> None:
    if not binary.is_file():
        fail(f"binary does not exist: {binary}")
    strings = tool_output(["strings", str(binary)])
    symbols = tool_output(["nm", "-D", str(binary)])
    if "mgba-gba-link-replicated-v2" not in strings:
        fail("binary lacks protocol-v2 identity")
    for forbidden in (
        "mgba-gba-link-netplay-v1",
        "GBASIONetplayDriver",
        "mLibretroNetpacketRegister",
        "netplayV1Diagnostic",
    ):
        if forbidden in strings or forbidden in symbols:
            fail(f"retired binary surface remains: {forbidden}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--binary", type=Path)
    args = parser.parse_args()
    audit_source()
    if args.build_dir:
        audit_generated_targets(args.build_dir.resolve())
    if args.binary:
        audit_binary(args.binary.resolve())
    print("protocol-v1 absence audit: passed")


if __name__ == "__main__":
    main()
