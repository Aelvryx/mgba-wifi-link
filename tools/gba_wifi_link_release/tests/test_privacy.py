"""Public-tree membership and category-only privacy validation tests."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.gba_wifi_link_release.admission import REQUIRED_GATES, REQUIRED_WORKFLOW
from tools.gba_wifi_link_release.model import BuildEvidence, GateResult, ReleaseAsset, ReleaseContext, load_contract
from tools.gba_wifi_link_release.privacy import PrivacyError, validate_public_tree
from tools.gba_wifi_link_release.provenance import canonical_json, release_provenance


ROOT = Path(__file__).resolve().parents[3]
CONTRACT = load_contract(ROOT / "packaging/gba-wifi-link/release/contract-v1.json")
BUILD_EVIDENCE = BuildEvidence(
    runner_image="ubuntu-24.04-20260125.1",
    pinned_actions=("actions/checkout@0123456789abcdef0123456789abcdef01234567",),
    pinned_toolchains=("android-ndk@27.2.12479018+sha256:" + "d" * 64,),
    configuration=(("android_abi", "arm64-v8a"), ("android_api", "21")),
)

CONTEXT = ReleaseContext(
    repository="Aelvryx/mgba-wifi-link",
    tag="v9.8.7",
    tag_object="a" * 40,
    commit="b" * 40,
    version="9.8.7",
    source_date_epoch=1_700_000_000,
    prerelease=True,
    gates=tuple(
        GateResult(name, REQUIRED_WORKFLOW, index + 100, index + 200, "success")
        for index, name in enumerate(REQUIRED_GATES)
    ),
    notes_sha256="c" * 64,
    build=BUILD_EVIDENCE,
)


class PrivacyTest(unittest.TestCase):
    def public_tree(self) -> tempfile.TemporaryDirectory[str]:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        contents = {
            "mgba_libretro_android.so": b"public core\n",
            "gba-link-test.gba": b"public test fixture\n",
            "gba-link-continuous.gba": b"public continuous fixture\n",
            "INSTALL-AND-USAGE.md": b"# Public usage\n",
            "mgba-gba-wifi-link-v9.8.7-android-arm64.zip": b"public archive\n",
            "SHA256SUMS": b"public checksums\n",
        }
        for name, data in contents.items():
            (root / name).write_bytes(data)
        payloads = tuple(
            ReleaseAsset(name, len(data), hashlib.sha256(data).hexdigest())
            for name, data in contents.items()
            if name not in {"SHA256SUMS"}
        )
        (root / "RELEASE-PROVENANCE.json").write_bytes(release_provenance(CONTEXT, payloads))
        return temporary

    def assert_category_only(self, category: str, target: Path, canary: str) -> None:
        with self.assertRaises(PrivacyError) as raised:
            validate_public_tree(target, CONTRACT)
        self.assertEqual(str(raised.exception), category)
        self.assertNotIn(canary, str(raised.exception))

    def test_declared_public_tree_is_accepted(self):
        with self.public_tree() as temporary:
            validate_public_tree(Path(temporary), CONTRACT)

    def test_public_https_url_is_not_misclassified_as_a_private_path(self):
        with self.public_tree() as temporary:
            root = Path(temporary)
            (root / "INSTALL-AND-USAGE.md").write_text(
                "Read https://github.com/Aelvryx/mgba-wifi-link/releases.\n",
                encoding="utf-8",
            )
            payloads = tuple(
                ReleaseAsset(name, len((root / name).read_bytes()),
                             hashlib.sha256((root / name).read_bytes()).hexdigest())
                for name in (
                    "mgba_libretro_android.so",
                    "gba-link-test.gba",
                    "gba-link-continuous.gba",
                    "INSTALL-AND-USAGE.md",
                    "mgba-gba-wifi-link-v9.8.7-android-arm64.zip",
                )
            )
            (root / "RELEASE-PROVENANCE.json").write_bytes(release_provenance(CONTEXT, payloads))
            validate_public_tree(root, CONTRACT)

    def test_release_provenance_requires_canonical_json_and_typed_source_fields(self):
        with self.public_tree() as temporary:
            root = Path(temporary)
            document = json.loads((root / "RELEASE-PROVENANCE.json").read_bytes())
            document["source"]["repository"] = False
            (root / "RELEASE-PROVENANCE.json").write_bytes(canonical_json(document))
            self.assert_category_only("PRIVACY_FIELD", root, "False")
        with self.public_tree() as temporary:
            root = Path(temporary)
            document = json.loads((root / "RELEASE-PROVENANCE.json").read_bytes())
            document["source"]["tag"] = "v9.8.8"
            document["source"]["version"] = "9.8.8"
            (root / "RELEASE-PROVENANCE.json").write_bytes(canonical_json(document))
            self.assert_category_only("PRIVACY_FIELD", root, "v9.8.8")
        with self.public_tree() as temporary:
            root = Path(temporary)
            document = json.loads((root / "RELEASE-PROVENANCE.json").read_bytes())
            (root / "RELEASE-PROVENANCE.json").write_text(
                json.dumps(document, indent=2) + "\n", encoding="utf-8"
            )
            self.assert_category_only("PRIVACY_JSON", root, "  \"schema\"")

    def test_private_text_canaries_return_only_stable_categories(self):
        cases = (
            ("PRIVACY_ROM_BIOS", "ROM identity: SYNTHETIC_ROM_BIOS_CANARY"),
            ("PRIVACY_ROM_BIOS", "ROM CRC: SYNTHETIC_ROM_CRC_CANARY"),
            ("PRIVACY_SAVE", "Save file: SYNTHETIC_SAVE_CANARY"),
            ("PRIVACY_SAVE", "Save identity: SYNTHETIC_SAVE_IDENTITY_CANARY"),
            ("PRIVACY_INPUT", "Raw input: SYNTHETIC_RAW_INPUT_CANARY"),
            ("PRIVACY_LOG", "Frontend log: SYNTHETIC_LOG_CANARY"),
            ("PRIVACY_PATH", "/private/SYNTHETIC_PATH_CANARY"),
            ("PRIVACY_ADDRESS", "198.51.100.77 SYNTHETIC_IPV4_CANARY"),
            ("PRIVACY_ADDRESS", "2001:db8::77 SYNTHETIC_IPV6_CANARY"),
            ("PRIVACY_ADDRESS", "02:00:00:00:00:77 SYNTHETIC_MAC_CANARY"),
            ("PRIVACY_DEVICE", "Device nickname: SYNTHETIC_NICKNAME_CANARY"),
            ("PRIVACY_COMMERCIAL", "Commercial evidence: SYNTHETIC_COMMERCIAL_CANARY"),
            ("PRIVACY_SECRET", "token=SYNTHETIC_SECRET_CANARY"),
        )
        for category, canary in cases:
            with self.subTest(category=category):
                with self.public_tree() as temporary:
                    root = Path(temporary)
                    (root / "INSTALL-AND-USAGE.md").write_text(canary + "\n", encoding="utf-8")
                    self.assert_category_only(category, root, canary)

    def test_private_url_query_and_fragment_paths_are_not_exempted(self):
        for canary in (
            "https://github.com/Aelvryx/mgba-wifi-link?path=%2Fprivate%2FSYNTHETIC_URL_QUERY_CANARY",
            "https://github.com/Aelvryx/mgba-wifi-link#path=%2Fprivate%2FSYNTHETIC_URL_FRAGMENT_CANARY",
        ):
            with self.subTest(canary=canary):
                with self.public_tree() as temporary:
                    root = Path(temporary)
                    (root / "INSTALL-AND-USAGE.md").write_text(canary + "\n", encoding="utf-8")
                    self.assert_category_only("PRIVACY_PATH", root, canary)

    def test_symlink_and_undeclared_file_are_rejected_without_disclosing_names(self):
        symlink_canary = "SYNTHETIC_SYMLINK_CANARY"
        with self.public_tree() as temporary:
            root = Path(temporary)
            (root / "INSTALL-AND-USAGE.md").unlink()
            (root / "INSTALL-AND-USAGE.md").symlink_to(symlink_canary)
            self.assert_category_only("PRIVACY_FILE_TYPE", root, symlink_canary)
        undeclared_canary = "SYNTHETIC_UNDECLARED_CANARY"
        with self.public_tree() as temporary:
            root = Path(temporary)
            (root / undeclared_canary).write_text("public-looking", encoding="utf-8")
            self.assert_category_only("PRIVACY_FILE_SET", root, undeclared_canary)
