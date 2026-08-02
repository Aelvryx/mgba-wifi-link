#!/usr/bin/env python3
"""Verify the active GBA Wi-Fi Link product/session/wire boundary."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

PRODUCT_ID = "mgba-gba-wifi-link"
PROTOCOL_ID = "mgba-gba-link-replicated-v2"

PRODUCT_PATHS = (
    "src/platform/libretro/gba-wifi-link.c",
    "src/platform/libretro/gba-wifi-link.h",
    "src/platform/test/libretro-gba-wifi-link.c",
    "src/platform/test/libretro-gba-wifi-link-replay.c",
)

VERSIONED_PATHS = (
    "include/mgba/internal/gba/sio/netplay/session-v2.h",
    "src/gba/sio/netplay/session-v2.c",
    "include/mgba/internal/gba/sio/netplay/protocol-v2.h",
    "src/gba/sio/netplay/protocol-v2.c",
)

RETIRED_REUSE_PATHS = (
    "include/mgba/internal/gba/sio/netplay/session.h",
    "src/gba/sio/netplay/session.c",
    "src/platform/libretro/netpacket.h",
    "src/platform/libretro/netpacket.c",
)

OBSOLETE_PRODUCT_PATHS = (
    "src/platform/libretro/netpacket-v2.h",
    "src/platform/libretro/netpacket-v2.c",
    "src/platform/test/libretro-netpacket-v2.c",
    "src/platform/test/libretro-netpacket-v2-replay.c",
    "src/gba/test/replicated-pair-spike.c",
    "src/platform/libretro/replicated-pair-spike.h",
    "src/platform/libretro/replicated-pair-spike.c",
    "src/platform/test/libretro-replicated-pair-spike.c",
    "tools/netpacket-spike",
)

CURRENT_SURFACES = (
    "src/platform/libretro",
    "src/platform/test",
    "src/gba/CMakeLists.txt",
    "CMakeLists.txt",
    ".github/workflows/gba-wifi-link-ci.yml",
    "tools/gba-wifi-link-qualification",
    "tools/four-swords-discovery",
    "tools/analyze-gba-wifi-link.py",
    "tools/test-analyze-gba-wifi-link.py",
    "README.md",
    "ROADMAP.md",
    "UPSTREAM.md",
    "docs/gba-wifi-link.md",
    "docs/gba-wifi-link-validation-matrix.md",
)

OBSOLETE_PRODUCT_TEXT = (
    "mLibretroNetpacketV2",
    "M_LIBRETRO_NETPACKET_V2",
    "netpacket-v2",
    "libretro-netpacket-v2",
    "replicated-pair-spike",
    "netpacket-spike",
    "GBA replicated link",
    "GBA Link Netplay Latency",
)

RETIRED_SYMBOL_PATTERNS = (
    re.compile(r"\bGBALinkSession(?:\b|[A-Z_])"),
    re.compile(r"\bGBA_LINK_SESSION_"),
    re.compile(r"\bmLibretroNetpacket(?!V2)"),
)

EXPECTED_TARGETS = (
    "test-gba-netplay-protocol-v2",
    "test-gba-netplay-session-v2",
    "test-gba-replicated-pair-scheduler",
    "test-libretro-replicated-pair-frontend",
    "test-libretro-gba-wifi-link",
    "test-libretro-gba-wifi-link-replay",
)

FORBIDDEN_TARGETS = (
    "test-gba-netplay-session",
    "test-libretro-netpacket",
    "test-libretro-netpacket-v2",
    "test-libretro-netpacket-v2-replay",
    "test-gba-replicated-pair-spike",
    "test-libretro-replicated-pair-spike",
)

TEXT_SUFFIXES = {
    ".c",
    ".h",
    ".cmake",
    ".cfg",
    ".json",
    ".md",
    ".opt",
    ".py",
    ".sh",
    ".txt",
    ".yaml",
    ".yml",
}


def fail(message: str) -> None:
    print(f"GBA Wi-Fi Link boundary audit: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace")


def current_files() -> list[Path]:
    files: list[Path] = []
    for relative in CURRENT_SURFACES:
        path = ROOT / relative
        if not path.exists():
            fail(f"current surface does not exist: {relative}")
        candidates = path.rglob("*") if path.is_dir() else (path,)
        for candidate in candidates:
            if not candidate.is_file():
                continue
            if candidate.resolve() == Path(__file__).resolve():
                continue
            if (
                candidate.suffix.lower() in TEXT_SUFFIXES
                or candidate.name == "CMakeLists.txt"
            ):
                files.append(candidate)
    return files


def audit_source() -> None:
    for relative in PRODUCT_PATHS + VERSIONED_PATHS:
        if not (ROOT / relative).is_file():
            fail(f"required boundary path is missing: {relative}")
    for relative in RETIRED_REUSE_PATHS + OBSOLETE_PRODUCT_PATHS:
        if (ROOT / relative).exists():
            fail(f"retired or obsolete product path exists: {relative}")

    libretro = read("src/platform/libretro/libretro.c")
    options = read("src/platform/libretro/libretro_core_options.h")
    facade_header = read("src/platform/libretro/gba-wifi-link.h")
    facade_source = read("src/platform/libretro/gba-wifi-link.c")
    session_header = read(
        "include/mgba/internal/gba/sio/netplay/session-v2.h"
    )
    protocol_header = read(
        "include/mgba/internal/gba/sio/netplay/protocol-v2.h"
    )

    if libretro.count("mLibretroGBAWifiLinkRegister(") != 1:
        fail("libretro does not have exactly one canonical registration call")
    if '#include "gba-wifi-link.h"' not in libretro:
        fail("libretro does not include the canonical product façade")
    if "mgba_gba_link_netplay_runtime" in libretro + options:
        fail("the retired runtime selector is declared or queried")
    if PRODUCT_ID not in facade_header + facade_source:
        fail("canonical product identity is missing from the façade")
    if "GBALinkV2Session" not in session_header:
        fail("the concrete session lost its version-qualified API")
    if '#include <mgba/internal/gba/sio/netplay/protocol-v2.h>' not in session_header:
        fail("the versioned session no longer owns the versioned codec boundary")
    for expected in (
        f'#define GBA_LINK_V2_PROTOCOL_NAME "{PROTOCOL_ID}"',
        "#define GBA_LINK_V2_PROTOCOL_MAGIC 0x32524C47",
        "#define GBA_LINK_V2_PROTOCOL_VERSION 2",
        "#define GBA_LINK_V2_RUNTIME_COMPATIBILITY_VERSION 2",
    ):
        if expected not in protocol_header:
            fail(f"versioned protocol constant changed or moved: {expected}")

    for path in current_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        relative = path.relative_to(ROOT)
        for obsolete in OBSOLETE_PRODUCT_TEXT:
            if obsolete in text:
                fail(f"obsolete product text {obsolete!r} in {relative}")
        for pattern in RETIRED_SYMBOL_PATTERNS:
            if pattern.search(text):
                fail(f"retired v1 symbol identity in {relative}: {pattern.pattern}")


def command_output(command: list[str]) -> str:
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


def audit_generated_targets(build_dir: Path) -> None:
    output = command_output(
        ["cmake", "--build", str(build_dir), "--target", "help"]
    )
    for target in EXPECTED_TARGETS:
        if not re.search(rf"(?m)^\s*{re.escape(target)}(?:\s|:|$)", output):
            fail(f"canonical generated target is missing: {target}")
    for target in FORBIDDEN_TARGETS:
        if re.search(rf"(?m)^\s*{re.escape(target)}(?:\s|:|$)", output):
            fail(f"obsolete or retired generated target exists: {target}")


def audit_binary(binary: Path) -> None:
    if not binary.is_file():
        fail(f"binary does not exist: {binary}")
    strings = command_output(["strings", str(binary)])
    symbols = command_output(["nm", "-D", str(binary)])
    for identity in (PRODUCT_ID, PROTOCOL_ID):
        if identity not in strings:
            fail(f"Android binary lacks required identity: {identity}")
    for forbidden in (
        "mLibretroNetpacketRegister",
        "mLibretroNetpacketV2",
        "GBA replicated link",
        "registered replicated-pair Netpacket protocol v2",
    ):
        if forbidden in strings or forbidden in symbols:
            fail(f"Android binary contains obsolete product identity: {forbidden}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--binary-only", action="store_true")
    args = parser.parse_args()
    if args.binary_only:
        if not args.binary or args.build_dir:
            fail("--binary-only requires --binary and excludes --build-dir")
        audit_binary(args.binary.resolve())
        print("GBA Wi-Fi Link boundary audit: passed")
        return
    audit_source()
    if args.build_dir:
        audit_generated_targets(args.build_dir.resolve())
    if args.binary:
        audit_binary(args.binary.resolve())
    print("GBA Wi-Fi Link boundary audit: passed")


if __name__ == "__main__":
    main()
