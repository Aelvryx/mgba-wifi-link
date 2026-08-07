#!/usr/bin/env python3
"""Verify the active GBA Wi-Fi Link product/session/wire boundary."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
POLICY_PATH = Path(__file__).with_name("gba-wifi-link-boundary-policy.json")

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

CURRENT_SURFACES = (
    "include/mgba/internal/gba/sio/netplay",
    "src/gba/sio/netplay",
    "src/gba/test",
    "src/gba/CMakeLists.txt",
    "src/platform/libretro",
    "src/platform/test",
    "CMakeLists.txt",
    ".github/workflows/gba-wifi-link-ci.yml",
    "tools",
    "README.md",
    "ROADMAP.md",
    "UPSTREAM.md",
    "docs/gba-wifi-link.md",
    "docs/gba-wifi-link-validation-matrix.md",
    "docs/gba-link-protocol-v2.md",
    "docs/gba-wifi-link-integration.md",
    "docs/protocol-v1-retirement.md",
    "openspec/specs",
)

HISTORICAL_OR_MIGRATION_PATHS = (
    "docs/protocol-v1-retirement.md",
    "docs/gba-wifi-link-integration.md",
    "docs/gba-wifi-link-validation-matrix.md",
    "openspec/changes/integrate-replicated-link-runtime/",
)

VERSIONED_IMPLEMENTATION_PREFIXES = (
    "include/mgba/internal/gba/sio/netplay/",
    "src/gba/sio/netplay/",
    "src/gba/test/",
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

EXPECTED_TARGETS = (
    "test-gba-netplay-protocol-v2",
    "test-gba-netplay-session-v2",
    "test-gba-replicated-pair-scheduler",
    "test-libretro-replicated-pair-frontend",
    "test-libretro-gba-wifi-link",
    "test-libretro-gba-wifi-link-replay",
)

VERSIONED_TOKEN = re.compile(
    r"protocol-v2|session-v2|GBALinkV2|GBA_LINK_V2|"
    r"(?<![A-Za-z0-9_])V2(?![A-Za-z0-9_])|"
    r"(?<![A-Za-z0-9_])v2(?![A-Za-z0-9_])"
)


def fail(message: str) -> None:
    print(f"GBA Wi-Fi Link boundary audit: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_policy(path: Path = POLICY_PATH) -> dict[str, list[str]]:
    try:
        value: Any = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read boundary policy {path}: {error}")
    if not isinstance(value, dict):
        fail("boundary policy root must be an object")
    required = {
        "retired_paths",
        "retired_symbol_families",
        "retired_targets",
        "retired_configuration_identities",
        "retired_compatibility_strings",
        "obsolete_product_paths",
        "obsolete_product_text",
        "obsolete_targets",
        "current_release_surfaces",
    }
    if set(value) != required:
        fail("boundary policy fields do not match the required inventory")
    for name, entries in value.items():
        if not isinstance(entries, list) or not entries or not all(
            isinstance(entry, str) and entry for entry in entries
        ):
            fail(f"boundary policy {name} must be a non-empty string list")
        if len(entries) != len(set(entries)):
            fail(f"boundary policy {name} contains duplicates")
    return value


POLICY = load_policy()

RELEASE_SURFACES = tuple(POLICY["current_release_surfaces"])

GUIDANCE_FILES = (
    "README.md",
    "ROADMAP.md",
    "SUPPORT.md",
    "docs/gba-wifi-link-release.md",
    ".github/ISSUE_TEMPLATE/bug.yml",
    ".github/ISSUE_TEMPLATE/compatibility.yml",
    "packaging/gba-wifi-link/release/templates/INSTALL-AND-USAGE.md.in",
)

GUIDANCE_REQUIRED = {
    "ROADMAP.md": (
        "## Now: v0.2.1 Maintainable alpha",
        "issues #21, #22, and #23",
        "Issue #20 is not a v0.2.1 exit gate.",
    ),
    "SUPPORT.md": (
        "The project neither solicits nor forbids unsolicited feedback",
        "does not promise a response or support service",
    ),
    "docs/gba-wifi-link-release.md": (
        "There is no second approval, dispatch, or **Publish** click.",
        "Pushing that annotated tag is the entire production release action.",
        "historical v0.2.0 in-place documentation correction",
    ),
}

GUIDANCE_FORBIDDEN = (
    ("supportable alpha", "obsolete supportable alpha wording"),
    ("please report", "feedback solicitation"),
    ("reports are welcome", "feedback solicitation"),
    ("issues #20–#23 are complete", "issue #20 release gate"),
)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace")


def _is_historical_or_migration(relative: str) -> bool:
    return relative.startswith(HISTORICAL_OR_MIGRATION_PATHS)


def _retired_reference_allowed(relative: str, token: str, line: str) -> bool:
    if _is_historical_or_migration(relative):
        return True
    if relative.startswith("openspec/specs/gba-wifi-link-runtime/"):
        return True
    if relative == "docs/gba-wifi-link.md" and token == (
        "mgba_gba_link_netplay_runtime"
    ):
        return True
    if relative == "src/gba/test/netplay-legacy-wire-fixture.h":
        return True
    return False


def _obsolete_reference_allowed(relative: str, token: str, line: str) -> bool:
    if _is_historical_or_migration(relative):
        return True
    return relative.startswith("openspec/specs/gba-wifi-link-runtime/")


def _versioned_token_allowed(relative: str, token: str, line: str) -> bool:
    if relative == "src/platform/libretro/libretro.h":
        # Upstream libretro API/VFS version names are unrelated to this product.
        return True
    if relative.startswith("src/platform/libretro/libretro_core_options"):
        return bool(
            re.search(
                r"retro_core_option|core options v2 interface|"
                r"SET_CORE_OPTIONS_V2",
                line,
                re.IGNORECASE,
            )
        )
    if relative.startswith(VERSIONED_IMPLEMENTATION_PREFIXES):
        return True
    if relative.startswith("openspec/specs/") or relative.startswith(
        "openspec/changes/integrate-replicated-link-runtime/"
    ):
        return True
    if relative in (
        "docs/gba-link-protocol-v2.md",
        "docs/gba-wifi-link-validation-matrix.md",
        "docs/gba-wifi-link-integration.md",
        "docs/protocol-v1-retirement.md",
        "UPSTREAM.md",
    ):
        return True
    if relative in PRODUCT_PATHS:
        return token in {
            "protocol-v2",
            "session-v2",
            "GBALinkV2",
            "GBA_LINK_V2",
        } or PROTOCOL_ID in line
    if relative.startswith("tools/"):
        return (
            PROTOCOL_ID in line
            or "run-v2" in line
            or "test-gba-netplay-protocol-v2" in line
            or "test-gba-netplay-session-v2" in line
        )
    if relative in ("CMakeLists.txt", "src/gba/CMakeLists.txt") or relative.startswith(
        ".github/workflows/"
    ):
        return bool(
            re.search(
                r"sio/netplay/(?:protocol|session)-v2|"
                r"test/netplay-(?:protocol|session)-v2|"
                r"test-gba-netplay-(?:protocol|session)-v2",
                line,
            )
        )
    if relative == "README.md":
        return "Protocol-v2 design" in line or PROTOCOL_ID in line
    if relative == "docs/gba-wifi-link.md":
        return (
            PROTOCOL_ID in line
            or "protocol v2" in line.lower()
            or "protocol-v2" in line.lower()
        )
    return False


def _contains_family(text: str, family: str) -> bool:
    return bool(
        re.search(
            rf"(?<![A-Za-z0-9_]){re.escape(family)}",
            text,
        )
    )


def find_text_policy_violations(relative: str, text: str) -> list[str]:
    """Return current-boundary violations; exposed for the policy self-test."""
    violations: list[str] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        for token in POLICY["obsolete_product_text"]:
            if token in line and not _obsolete_reference_allowed(
                relative, token, line
            ):
                violations.append(
                    f"obsolete product text {token!r} at {relative}:{line_number}"
                )
        for token in (
            POLICY["retired_symbol_families"]
            + POLICY["retired_configuration_identities"]
            + POLICY["retired_compatibility_strings"]
            + POLICY["retired_targets"]
        ):
            if token in POLICY["retired_symbol_families"]:
                present = _contains_family(line, token)
            elif token in POLICY["retired_targets"]:
                present = bool(
                    re.search(
                        rf"(?<![A-Za-z0-9_-]){re.escape(token)}"
                        rf"(?![A-Za-z0-9_-])",
                        line,
                    )
                )
            else:
                present = token in line
            if present and not _retired_reference_allowed(relative, token, line):
                violations.append(
                    f"retired identity {token!r} at {relative}:{line_number}"
                )
        for match in VERSIONED_TOKEN.finditer(line):
            token = match.group(0)
            if not _versioned_token_allowed(relative, token, line):
                violations.append(
                    f"unclassified versioned token {token!r} at "
                    f"{relative}:{line_number}"
                )
    return violations


def find_guidance_policy_violations(relative: str, text: str) -> list[str]:
    """Return public-guidance violations without depending on display prose."""
    violations: list[str] = []
    lowered = text.lower()
    for phrase, description in GUIDANCE_FORBIDDEN:
        if phrase in lowered:
            violations.append(f"{description} at {relative}")
    if re.search(
        r"(?:must|required|require[sd]?).{0,32}(?:manual\s+)?publish",
        lowered,
    ):
        violations.append(f"manual Publish requirement at {relative}")
    return violations


def guidance_policy_violations() -> list[str]:
    """Verify the deliberately neutral public alpha/release guidance contract."""
    violations: list[str] = []
    for relative in GUIDANCE_FILES:
        path = ROOT / relative
        if not path.is_file():
            return [f"guidance surface is missing: {relative}"]
        violations.extend(find_guidance_policy_violations(relative, read(relative)))
    for relative, phrases in GUIDANCE_REQUIRED.items():
        text = re.sub(r"\s+", " ", read(relative))
        for phrase in phrases:
            if re.sub(r"\s+", " ", phrase) not in text:
                violations.append(f"missing guidance contract {phrase!r} in {relative}")
    return violations


def current_files() -> list[Path]:
    files: set[Path] = set()
    excluded = {
        Path(__file__).resolve(),
        POLICY_PATH.resolve(),
        Path(__file__).with_name("test-audit-gba-wifi-link-boundary.py").resolve(),
    }
    for relative in CURRENT_SURFACES + RELEASE_SURFACES:
        path = ROOT / relative
        if not path.exists():
            fail(f"current surface does not exist: {relative}")
        candidates = path.rglob("*") if path.is_dir() else (path,)
        for candidate in candidates:
            if not candidate.is_file() or candidate.resolve() in excluded:
                continue
            if "openspec/changes/archive" in candidate.as_posix():
                continue
            if candidate.suffix.lower() in TEXT_SUFFIXES or candidate.name == (
                "CMakeLists.txt"
            ):
                files.add(candidate)
    return sorted(files)


def audit_source() -> None:
    for relative in PRODUCT_PATHS + VERSIONED_PATHS:
        if not (ROOT / relative).is_file():
            fail(f"required boundary path is missing: {relative}")
    for relative in POLICY["retired_paths"] + POLICY["obsolete_product_paths"]:
        if (ROOT / relative).exists():
            fail(f"retired or obsolete product path exists: {relative}")

    libretro = read("src/platform/libretro/libretro.c")
    options = read("src/platform/libretro/libretro_core_options.h")
    facade_header = read("src/platform/libretro/gba-wifi-link.h")
    facade_source = read("src/platform/libretro/gba-wifi-link.c")
    session_header = read("include/mgba/internal/gba/sio/netplay/session-v2.h")
    protocol_header = read("include/mgba/internal/gba/sio/netplay/protocol-v2.h")

    if libretro.count("mLibretroGBAWifiLinkRegister(") != 1:
        fail("libretro does not have exactly one canonical registration call")
    if '#include "gba-wifi-link.h"' not in libretro:
        fail("libretro does not include the canonical product façade")
    if "mgba_gba_link_netplay_runtime" in libretro + options:
        fail("the retired runtime selector is declared or queried")
    if PRODUCT_ID not in facade_header + facade_source:
        fail("canonical product identity is missing from the façade")
    if "product schema=" not in facade_source or "failure schema=" not in facade_source:
        fail("the façade lacks stable structured product/failure diagnostics")
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
        relative = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        violations = find_text_policy_violations(relative, text)
        if violations:
            fail(violations[0])
    guidance_violations = guidance_policy_violations()
    if guidance_violations:
        fail(guidance_violations[0])


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


def _target_present(output: str, target: str) -> bool:
    return bool(re.search(rf"(?m)^\s*{re.escape(target)}(?:\s|:|$)", output))


def audit_generated_targets(build_dir: Path) -> None:
    output = command_output(["cmake", "--build", str(build_dir), "--target", "help"])
    for target in EXPECTED_TARGETS:
        if not _target_present(output, target):
            fail(f"canonical generated target is missing: {target}")
    for target in POLICY["retired_targets"] + POLICY["obsolete_targets"]:
        if _target_present(output, target):
            fail(f"obsolete or retired generated target exists: {target}")


def audit_binary(binary: Path) -> None:
    if not binary.is_file():
        fail(f"binary does not exist: {binary}")
    strings = command_output(["strings", str(binary)])
    symbols = command_output(["nm", "-D", str(binary)])
    for identity in (PRODUCT_ID, PROTOCOL_ID, "product schema=", "failure schema="):
        if identity not in strings:
            fail(f"Android binary lacks required identity: {identity}")
    for forbidden in (
        POLICY["retired_symbol_families"]
        + POLICY["retired_compatibility_strings"]
        + POLICY["obsolete_product_text"]
    ):
        if forbidden in strings or forbidden in symbols:
            fail(f"Android binary contains retired or obsolete identity: {forbidden}")


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
