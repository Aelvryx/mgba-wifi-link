"""Release package construction contracts."""

from dataclasses import replace
import hashlib
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO

from tools.gba_wifi_link_release.admission import REQUIRED_GATES, REQUIRED_WORKFLOW
from tools.gba_wifi_link_release.model import BuildEvidence, GateResult, ReleaseContext
from tools.gba_wifi_link_release.packager import (
    PackageError,
    PackageInputs,
    build_release,
)


ROOT = Path(__file__).resolve().parents[3]
TEMPLATES = ROOT / "packaging/gba-wifi-link/release/templates"


def context(epoch: int = 1_700_000_000) -> ReleaseContext:
    return ReleaseContext(
        repository="Aelvryx/mgba-wifi-link",
        tag="v9.8.7",
        tag_object="a" * 40,
        commit="b" * 40,
        version="9.8.7",
        source_date_epoch=epoch,
        prerelease=True,
        gates=tuple(GateResult(name, REQUIRED_WORKFLOW, index + 1, index + 10, "success")
                    for index, name in enumerate(REQUIRED_GATES)),
        notes_sha256=hashlib.sha256(b"Reviewed synthetic release notes.\n").hexdigest(),
        build=BuildEvidence(
            "synthetic-runner-2026",
            ("actions/checkout@0123456789abcdef0123456789abcdef01234567",),
            ("android-ndk@27.2.12479018+sha256:" + "d" * 64,),
            (("android_abi", "arm64-v8a"), ("android_api", "21")),
        ),
    )


class PackageTest(unittest.TestCase):
    def make_inputs(self, root: Path) -> PackageInputs:
        source = root / "input"
        source.mkdir()
        (source / "mgba_libretro_android.so").write_bytes(b"synthetic core\r\n")
        (source / "gba-link-test.gba").write_bytes(b"test fixture\n")
        (source / "gba-link-continuous.gba").write_bytes(b"continuous fixture\n")
        (source / "LICENSE").write_bytes(b"licence trailing  \r\nsecond\t\r\n")
        return PackageInputs(
            core=source / "mgba_libretro_android.so",
            test_fixture=source / "gba-link-test.gba",
            continuous_fixture=source / "gba-link-continuous.gba",
            licence=source / "LICENSE",
            install_template=TEMPLATES / "INSTALL-AND-USAGE.md.in",
            source_template=TEMPLATES / "SOURCE-AND-PROVENANCE.md.in",
            release_notes=b"Reviewed synthetic release notes.\n",
        )

    def test_build_emits_full_ordered_public_set_and_exact_checksum_scopes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            release = build_release(context(), self.make_inputs(root), root / "release")
            names = tuple(asset.name for asset in release.assets)
            self.assertEqual(names, (
                "mgba_libretro_android.so", "gba-link-test.gba", "gba-link-continuous.gba",
                "INSTALL-AND-USAGE.md", "mgba-gba-wifi-link-v9.8.7-android-arm64.zip",
                "RELEASE-PROVENANCE.json", "SHA256SUMS",
            ))
            sums = (root / "release/SHA256SUMS").read_text(encoding="utf-8").splitlines()
            self.assertEqual(tuple(line.split("  ", 1)[1] for line in sums), names[:-1])
            with zipfile.ZipFile(root / "release" / names[4]) as archive:
                internal_sums = archive.read("SHA256SUMS").decode("utf-8").splitlines()
            self.assertEqual(tuple(line.split("  ", 1)[1] for line in internal_sums), (
                "BUILD-PROVENANCE.json", "INSTALL-AND-USAGE.md", "LICENSE",
                "SOURCE-AND-PROVENANCE.md", "gba-link-continuous.gba",
                "gba-link-test.gba", "mgba_libretro_android.so",
            ))

    def test_build_normalizes_text_and_zip_metadata_deterministically(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build_release(context(), self.make_inputs(root), root / "release")
            release = root / "release"
            for name in ("INSTALL-AND-USAGE.md", "RELEASE-PROVENANCE.json", "SHA256SUMS"):
                contents = (release / name).read_bytes()
                self.assertTrue(contents.endswith(b"\n"))
                self.assertNotIn(b"\r", contents)
            provenance = (release / "RELEASE-PROVENANCE.json").read_bytes()
            self.assertEqual(provenance, json.dumps(json.loads(provenance), sort_keys=True,
                                                    separators=(",", ":")).encode() + b"\n")
            with zipfile.ZipFile(release / "mgba-gba-wifi-link-v9.8.7-android-arm64.zip") as archive:
                members = archive.infolist()
                self.assertEqual(tuple(member.filename for member in members), tuple(sorted(member.filename for member in members)))
                self.assertTrue(all(member.date_time == (2023, 11, 14, 22, 13, 20) for member in members))
                self.assertTrue(all(member.create_system == 3 for member in members))
                self.assertTrue(all((member.external_attr >> 16) == 0o100644 for member in members))
                self.assertTrue(all(member.extra == b"" and member.comment == b"" for member in members))
                self.assertTrue(all(member.compress_type == zipfile.ZIP_DEFLATED for member in members))
            self.assertTrue(all(stat.S_IMODE(path.stat().st_mode) == 0o644 for path in release.iterdir()))

    def test_build_rejects_unrepresentable_zip_epoch_and_unsafe_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = self.make_inputs(root)
            with self.assertRaisesRegex(PackageError, "^PACKAGE_EPOCH$"):
                build_release(context(0), inputs, root / "release")
            (root / "input/gba-link-test.gba").unlink()
            with self.assertRaisesRegex(PackageError, "^PACKAGE_INPUT$"):
                build_release(context(), inputs, root / "missing")
            (root / "input/gba-link-test.gba").symlink_to(root / "input/gba-link-continuous.gba")
            with self.assertRaisesRegex(PackageError, "^PACKAGE_INPUT$"):
                build_release(context(), inputs, root / "symlink")

    def test_build_refuses_existing_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "release"
            output.mkdir()
            with self.assertRaisesRegex(PackageError, "^PACKAGE_OUTPUT$"):
                build_release(context(), self.make_inputs(root), output)

    def test_build_rejects_notes_not_bound_to_admitted_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = self.make_inputs(root)
            with self.assertRaisesRegex(PackageError, "^PACKAGE_INPUT$"):
                build_release(context(), replace(inputs, release_notes=b"Substituted notes.\n"), root / "release")

    def test_cli_builds_atomically_and_exposes_nonpublishing_commands(self):
        from tools.gba_wifi_link_release.cli import main

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "release"
            self.assertEqual(main(["build", "--fixture", "synthetic", "--output", str(output)]), 0)
            self.assertTrue(output.is_dir())
            self.assertEqual(main(["verify", "--fixture", "synthetic", "--output", str(output)]), 0)
            rendered = StringIO()
            with redirect_stdout(rendered):
                self.assertEqual(main(["render-body", "--fixture", "synthetic"]), 0)
            self.assertIn("# mGBA GBA Wi-Fi Link v9.8.7", rendered.getvalue())
            self.assertNotIn("publish", rendered.getvalue().casefold())
            with redirect_stderr(StringIO()):
                self.assertNotEqual(main(["build", "--fixture", "synthetic", "--output", str(output)]), 0)

    def test_standalone_cli_script_imports_from_the_checkout_root(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                (sys.executable, str(ROOT / "tools/gba-wifi-link-release.py"), "build",
                 "--fixture", "synthetic", "--output", str(Path(directory) / "release")),
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
