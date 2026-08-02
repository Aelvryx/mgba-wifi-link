#!/usr/bin/env python3
"""Policy-level regressions for the permanent GBA Wi-Fi Link boundary audit."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("audit-gba-wifi-link-boundary.py")
SPEC = importlib.util.spec_from_file_location("gba_wifi_link_boundary_audit", SCRIPT)
assert SPEC and SPEC.loader
AUDIT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = AUDIT
SPEC.loader.exec_module(AUDIT)


class BoundaryPolicyTest(unittest.TestCase):
    def assert_rejected(self, relative: str, text: str, needle: str) -> None:
        violations = AUDIT.find_text_policy_violations(relative, text)
        self.assertTrue(violations, f"policy unexpectedly accepted {relative}: {text}")
        self.assertTrue(
            any(needle in violation for violation in violations),
            f"{needle!r} absent from {violations!r}",
        )

    def assert_allowed(self, relative: str, text: str) -> None:
        self.assertEqual(AUDIT.find_text_policy_violations(relative, text), [])

    def test_product_facing_netpacket_v2_is_rejected(self) -> None:
        self.assert_rejected(
            "src/platform/libretro/gba-wifi-link.c",
            "static void netpacket-v2(void);",
            "obsolete product text",
        )

    def test_unapproved_product_v2_target_is_rejected(self) -> None:
        self.assert_rejected(
            "CMakeLists.txt",
            "add_executable(test-libretro-gba-wifi-link-v2 product.c)",
            "unclassified versioned token",
        )

    def test_retired_v1_symbol_reuse_is_rejected(self) -> None:
        self.assert_rejected(
            "src/platform/libretro/gba-wifi-link.c",
            "void GBASIONetplayDriverCreate(void);",
            "retired identity",
        )

    def test_versioned_setup_prose_is_rejected(self) -> None:
        self.assert_rejected(
            "README.md",
            "Select the V2 runtime before hosting.",
            "unclassified versioned token",
        )

    def test_facade_may_consume_versioned_session_api(self) -> None:
        self.assert_allowed(
            "src/platform/libretro/gba-wifi-link.c",
            "struct GBALinkV2Session session;",
        )

    def test_wire_reference_may_use_versioned_terminology(self) -> None:
        self.assert_allowed(
            "docs/gba-link-protocol-v2.md",
            "protocol-v2 uses GBALinkV2Packet and GBA_LINK_V2_MESSAGE_HELLO.",
        )

    def test_historical_inventory_may_name_retired_surfaces(self) -> None:
        self.assert_allowed(
            "docs/protocol-v1-retirement.md",
            "GBALinkSession and mLibretroNetpacket were retired.",
        )

    def test_authoritative_boundary_spec_may_name_forbidden_surface(self) -> None:
        self.assert_allowed(
            "openspec/specs/gba-wifi-link-runtime/spec.md",
            "The current product SHALL NOT use netpacket-v2.",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
