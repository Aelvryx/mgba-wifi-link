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
    def test_generated_provenance_is_self_contained_without_signed_attestations(self):
        raw = json.loads(CONTRACT.read_text(encoding="utf-8"))
        workflow = raw["release_workflow"]
        self.assertEqual(
            set(workflow["action_pins"]),
            {"actions/checkout", "actions/download-artifact", "actions/upload-artifact"},
        )
        self.assertEqual(set(workflow["action_versions"]), set(workflow["action_pins"]))
        source_template = (
            CONTRACT.parent / "templates/SOURCE-AND-PROVENANCE.md.in"
        ).read_text(encoding="utf-8")
        self.assertNotIn("attestation", source_template.casefold())
        self.assertIn("BUILD-PROVENANCE.json", source_template)

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
        self.assertEqual(contract.license_sha256, "1f256ecad192880510e84ad60474eab7589218784b9a50bc7ceee34c2b91f1d5")
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

    def test_contract_owns_conservative_public_and_archive_resource_budgets(self):
        contract = load_contract(CONTRACT)
        self.assertEqual(dict(contract.public_asset_max_bytes), {
            "INSTALL-AND-USAGE.md": 1_048_576,
            "RELEASE-PROVENANCE.json": 1_048_576,
            "SHA256SUMS": 65_536,
            "gba-link-continuous.gba": 1_048_576,
            "gba-link-test.gba": 1_048_576,
            "mgba-gba-wifi-link-{tag}-android-arm64.zip": 83_886_080,
            "mgba_libretro_android.so": 67_108_864,
        })
        self.assertEqual(contract.public_aggregate_max_bytes, 134_217_728)
        self.assertEqual(contract.archive_uncompressed_aggregate_max_bytes, 71_303_168)
        self.assertEqual(contract.archive_compressed_aggregate_max_bytes, 71_303_168)
        self.assertEqual(contract.archive_central_directory_max_bytes, 65_536)
        self.assertEqual(contract.archive_max_compression_ratio, 100)
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
