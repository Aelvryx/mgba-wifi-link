"""Fail-closed verification for deterministic release sets."""

from pathlib import Path
import shutil
import tempfile
import unittest
import zipfile

from tools.gba_wifi_link_release.packager import build_release
import tools.gba_wifi_link_release.tests.test_packager as package_tests
from tools.gba_wifi_link_release.verifier import VerificationError, verify_release


class VerifierTest(unittest.TestCase):
    def build(self, root: Path) -> Path:
        maker = package_tests.PackageTest()
        return_root = root / "release"
        build_release(package_tests.context(), maker.make_inputs(root), return_root)
        return return_root

    def assert_rejected(self, mutate) -> None:
        with tempfile.TemporaryDirectory() as directory:
            release = self.build(Path(directory))
            mutate(release)
            with self.assertRaisesRegex(VerificationError, "^VERIFY_"):
                verify_release(release, package_tests.context())

    def test_verifier_accepts_fresh_release_and_returns_full_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            release = self.build(Path(directory))
            verified = verify_release(release, package_tests.context())
            self.assertEqual(tuple(asset.name for asset in verified.assets), (
                "mgba_libretro_android.so", "gba-link-test.gba", "gba-link-continuous.gba",
                "INSTALL-AND-USAGE.md", "mgba-gba-wifi-link-v9.8.7-android-arm64.zip",
                "RELEASE-PROVENANCE.json", "SHA256SUMS",
            ))

    def test_verifier_rejects_missing_extra_renamed_and_nonregular_public_entries(self):
        self.assert_rejected(lambda root: (root / "LICENSE").write_text("extra\n", encoding="utf-8"))
        self.assert_rejected(lambda root: (root / "mgba_libretro_android.so").unlink())
        self.assert_rejected(lambda root: (root / "gba-link-test.gba").rename(root / "renamed.gba"))
        def symlink(root: Path) -> None:
            (root / "INSTALL-AND-USAGE.md").unlink()
            (root / "INSTALL-AND-USAGE.md").symlink_to(root / "LICENSE")
        self.assert_rejected(symlink)
        self.assert_rejected(lambda root: (root / "nested").mkdir())

    def test_verifier_rejects_digest_and_mode_changes(self):
        def wrong_digest(root: Path) -> None:
            with (root / "gba-link-test.gba").open("ab") as output:
                output.write(b"altered")
        self.assert_rejected(wrong_digest)
        self.assert_rejected(lambda root: (root / "gba-link-test.gba").chmod(0o600))

    def test_verifier_rejects_appended_member_and_unsafe_archive_metadata(self):
        def append_member(root: Path) -> None:
            archive_path = root / "mgba-gba-wifi-link-v9.8.7-android-arm64.zip"
            with zipfile.ZipFile(archive_path, "a") as archive:
                archive.writestr("extra.txt", b"extra")
        self.assert_rejected(append_member)

    def test_verifier_rejects_stale_and_cyclic_manifests(self):
        self.assert_rejected(lambda root: (root / "INSTALL-AND-USAGE.md").write_text("stale\n", encoding="utf-8"))
        def self_hash(root: Path) -> None:
            sums = root / "SHA256SUMS"
            sums.write_text(sums.read_text(encoding="utf-8") + "0" * 64 + "  SHA256SUMS\n", encoding="utf-8")
        self.assert_rejected(self_hash)
        def provenance_cycle(root: Path) -> None:
            provenance = root / "RELEASE-PROVENANCE.json"
            provenance.write_bytes(provenance.read_bytes().replace(b'"payloads"', b'"payloads":"SHA256SUMS","x"'))
        self.assert_rejected(provenance_cycle)
