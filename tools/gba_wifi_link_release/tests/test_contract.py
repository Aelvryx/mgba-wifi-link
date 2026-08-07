"""Regression tests for the immutable release artifact contract."""

from pathlib import Path
import json
import unittest

from tools.gba_wifi_link_release.model import load_contract


CONTRACT = (
    Path(__file__).resolve().parents[3]
    / "packaging/gba-wifi-link/release/contract-v1.json"
)


class ContractTest(unittest.TestCase):
    def test_contract_freezes_public_and_archive_members(self):
        contract = load_contract(CONTRACT)
        self.assertEqual(contract.schema, 1)
        self.assertEqual(len(contract.public_assets), 7)
        self.assertEqual(
            contract.public_assets[-2:],
            ("RELEASE-PROVENANCE.json", "SHA256SUMS"),
        )
        self.assertEqual(
            set(contract.archive_members),
            {
                "mgba_libretro_android.so",
                "gba-link-test.gba",
                "gba-link-continuous.gba",
                "INSTALL-AND-USAGE.md",
                "SOURCE-AND-PROVENANCE.md",
                "LICENSE",
                "BUILD-PROVENANCE.json",
                "SHA256SUMS",
            },
        )

    def test_synthetic_core_fixture_is_public_and_versioned(self):
        fixture = (
            Path(__file__).resolve().parents[1]
            / "fixtures/synthetic/input/mgba_libretro_android.so"
        )
        self.assertEqual(fixture.read_bytes(), b"synthetic core v9.8.7\n")

    def test_synthetic_license_matches_normalized_repository_mpl_2_0_text(self):
        repository = Path(__file__).resolve().parents[3]
        fixture = repository / "tools/gba_wifi_link_release/fixtures/synthetic/input/LICENSE"
        normalized_license = b"\n".join(
            line.rstrip(b" \t")
            for line in (repository / "LICENSE").read_bytes().splitlines()
        ) + b"\n"
        self.assertEqual(fixture.read_bytes(), normalized_license)

    def test_contract_exposes_normalized_archive_and_checksum_rules(self):
        contract = load_contract(CONTRACT)
        self.assertEqual(
            contract.archive_members,
            (
                "BUILD-PROVENANCE.json",
                "INSTALL-AND-USAGE.md",
                "LICENSE",
                "SHA256SUMS",
                "SOURCE-AND-PROVENANCE.md",
                "gba-link-continuous.gba",
                "gba-link-test.gba",
                "mgba_libretro_android.so",
            ),
        )
        self.assertEqual(contract.file_mode, "0644")
        self.assertEqual(
            contract.archive_sha256_members,
            (
                "BUILD-PROVENANCE.json",
                "INSTALL-AND-USAGE.md",
                "LICENSE",
                "SOURCE-AND-PROVENANCE.md",
                "gba-link-continuous.gba",
                "gba-link-test.gba",
                "mgba_libretro_android.so",
            ),
        )
        self.assertEqual(len(contract.release_provenance_assets), 5)
        self.assertEqual(len(contract.standalone_sha256_assets), 6)
        self.assertEqual(contract.zip_member_order, "lexicographic")

    def test_history_preserves_the_public_archive_inventory(self):
        history = Path(__file__).resolve().parents[1] / "fixtures/v0.2.0-history.json"
        data = json.loads(history.read_text(encoding="utf-8"))
        self.assertEqual(
            {member["name"] for member in data["archive_members"]},
            {
                "mgba_libretro_android.so",
                "gba-link-test.gba",
                "gba-link-continuous.gba",
                "INSTALL-AND-USAGE.md",
                "SOURCE-AND-PROVENANCE.md",
                "LICENSE",
                "SHA256SUMS",
            },
        )
        self.assertEqual(len(data["archive_sha256_members"]), 6)

    def test_synthetic_canaries_cover_each_private_material_category(self):
        canaries = Path(__file__).resolve().parents[1] / "fixtures/synthetic/canaries"
        self.assertEqual(
            {path.stem for path in canaries.glob("*.txt")},
            {
                "address",
                "commercial-evidence",
                "device-identity",
                "endpoint-log",
                "private-path",
                "raw-input",
                "rom-bios",
                "save",
                "secret",
            },
        )
        for canary in canaries.glob("*.txt"):
            self.assertTrue(canary.read_text(encoding="utf-8").startswith("SYNTHETIC_"))
