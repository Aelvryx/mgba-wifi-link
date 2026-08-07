"""Release package construction contracts."""

from dataclasses import replace
import hashlib
import json
import errno
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
from tools.gba_wifi_link_release.model import (
    ActualBuildEvidence,
    BuildEvidence,
    GateResult,
    ReleaseAsset,
    ReleaseContext,
)
from tools.gba_wifi_link_release.packager import (
    PackageError,
    PackageInputs,
    build_release,
    zip_timestamp,
)
from tools.gba_wifi_link_release.verifier import VerificationError


ROOT = Path(__file__).resolve().parents[3]
TEMPLATES = ROOT / "packaging/gba-wifi-link/release/templates"


def actual_builds(epoch: int) -> tuple[ActualBuildEvidence, ...]:
    core_bytes = b"synthetic core\r\n"
    core = ReleaseAsset(
        "mgba_libretro_android.so", len(core_bytes), hashlib.sha256(core_bytes).hexdigest(),
    )
    common = {
        "run_id": 900,
        "runner_image_os": "ubuntu24",
        "runner_image_version": "20260125.1",
        "ndk_revision": "27.2.12479018",
        "ndk_source_properties_sha256": "d" * 64,
        "compiler_sha256": "e" * 64,
        "compiler_version": "Android clang version 18.0.3",
        "cmake_version": "cmake version 3.31.6",
        "ninja_version": "1.12.1",
        "source_commit": "b" * 40,
        "source_date_epoch": epoch,
        "configuration": (("android_abi", "arm64-v8a"), ("android_api", "21")),
        "core": core,
    }
    checkout = "actions/checkout@v6+sha:0123456789abcdef0123456789abcdef01234567"
    upload = "actions/upload-artifact@v4+sha:89abcdef0123456789abcdef0123456789abcdef"
    return (
        ActualBuildEvidence(
            role="protected", job_id=901,
            pinned_actions=(checkout, "actions/download-artifact@v5+sha:" + "1" * 40, upload),
            **common,
        ),
        ActualBuildEvidence(
            role="independent", job_id=902, pinned_actions=(checkout, upload), **common,
        ),
    )


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
            (
                "actions/checkout@0123456789abcdef0123456789abcdef01234567",
                "actions/download-artifact@" + "1" * 40,
                "actions/upload-artifact@89abcdef0123456789abcdef0123456789abcdef",
            ),
            ("android-ndk@27.2.12479018+sha256:" + "d" * 64,),
            (("android_abi", "arm64-v8a"), ("android_api", "21")),
            actual_builds(epoch),
        ),
    )


class PackageTest(unittest.TestCase):
    def make_inputs(self, root: Path) -> PackageInputs:
        source = root / "input"
        source.mkdir()
        (source / "mgba_libretro_android.so").write_bytes(b"synthetic core\r\n")
        (source / "gba-link-test.gba").write_bytes(b"test fixture\n")
        (source / "gba-link-continuous.gba").write_bytes(b"continuous fixture\n")
        licence = b"\n".join(line.rstrip(b" \t") for line in (ROOT / "LICENSE").read_bytes().splitlines()) + b"\n"
        (source / "LICENSE").write_bytes(licence.replace(b"\n", b"\r\n"))
        install_template = source / "INSTALL-AND-USAGE.md.in"
        source_template = source / "SOURCE-AND-PROVENANCE.md.in"
        install_template.write_bytes(TEMPLATES.joinpath("INSTALL-AND-USAGE.md.in").read_bytes())
        source_template.write_bytes(TEMPLATES.joinpath("SOURCE-AND-PROVENANCE.md.in").read_bytes())
        return PackageInputs(
            core=source / "mgba_libretro_android.so",
            test_fixture=source / "gba-link-test.gba",
            continuous_fixture=source / "gba-link-continuous.gba",
            licence=source / "LICENSE",
            install_template=install_template,
            source_template=source_template,
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

    def test_zip_timestamp_uses_dos_two_second_precision_at_odd_and_boundary_epochs(self):
        self.assertEqual(zip_timestamp(1_700_000_001), (2023, 11, 14, 22, 13, 20))
        self.assertEqual(zip_timestamp(315_532_800), (1980, 1, 1, 0, 0, 0))
        self.assertEqual(zip_timestamp(4_354_819_199), (2107, 12, 31, 23, 59, 58))
        for epoch in (315_532_799, 4_354_819_200):
            with self.subTest(epoch=epoch):
                with self.assertRaisesRegex(PackageError, "^PACKAGE_EPOCH$"):
                    zip_timestamp(epoch)

    def test_build_freezes_normalized_mpl_license_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = self.make_inputs(root)
            (root / "input/LICENSE").write_text("substituted license\n", encoding="utf-8")
            with self.assertRaisesRegex(PackageError, "^PACKAGE_LICENCE$"):
                build_release(context(), inputs, root / "release")
            (root / "input/LICENSE").write_text("private /secret/license\n", encoding="utf-8")
            with self.assertRaisesRegex(PackageError, "^PACKAGE_LICENCE$"):
                build_release(context(), inputs, root / "private")

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

    def test_atomic_build_never_overwrites_a_destination_that_appears_before_install(self):
        from tools.gba_wifi_link_release.cli import _build_atomic

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "release"

            def racer() -> None:
                output.mkdir()
                (output / "racer-marker").write_text("preserve\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "^CLI_INSTALL$"):
                _build_atomic(context(), self.make_inputs(root), output, before_install=racer)
            self.assertEqual((output / "racer-marker").read_text(encoding="utf-8"), "preserve\n")

    def test_atomic_build_preserves_existing_empty_and_nonempty_destinations(self):
        from tools.gba_wifi_link_release.cli import _build_atomic

        for nonempty in (False, True):
            with self.subTest(nonempty=nonempty), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                output = root / "release"
                output.mkdir()
                if nonempty:
                    (output / "marker").write_text("keep\n", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "^CLI_OUTPUT$"):
                    _build_atomic(context(), self.make_inputs(root), output)
                self.assertEqual((output / "marker").exists(), nonempty)

    def test_atomic_build_leaves_no_final_path_when_no_replace_install_fails(self):
        from tools.gba_wifi_link_release.cli import _build_atomic

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "release"

            def fail_install(source: Path, destination: Path) -> None:
                raise OSError(errno.EIO, "injected install failure")

            with self.assertRaisesRegex(ValueError, "^CLI_INSTALL$"):
                _build_atomic(context(), self.make_inputs(root), output, renamer=fail_install)
            self.assertFalse(output.exists())

    def test_two_clean_synthetic_cli_runs_are_byte_identical(self):
        from tools.gba_wifi_link_release.cli import main

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            left, right = root / "left", root / "right"
            self.assertEqual(main(["build", "--fixture", "synthetic", "--output", str(left)]), 0)
            self.assertEqual(main(["build", "--fixture", "synthetic", "--output", str(right)]), 0)
            self.assertEqual(
                {path.name: path.read_bytes() for path in left.iterdir()},
                {path.name: path.read_bytes() for path in right.iterdir()},
            )

    def test_standalone_cli_script_imports_from_the_checkout_root(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                (sys.executable, str(ROOT / "tools/gba-wifi-link-release.py"), "build",
                 "--fixture", "synthetic", "--output", str(Path(directory) / "release")),
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_declared_inputs_have_exact_public_asset_impact_maps(self):
        cases = (
            ("core", b"changed core\n", None, {
                "mgba_libretro_android.so", "mgba-gba-wifi-link-v9.8.7-android-arm64.zip",
                "RELEASE-PROVENANCE.json", "SHA256SUMS",
            }),
            ("test_fixture", b"changed test fixture\n", None, {
                "gba-link-test.gba", "mgba-gba-wifi-link-v9.8.7-android-arm64.zip",
                "RELEASE-PROVENANCE.json", "SHA256SUMS",
            }),
            ("continuous_fixture", b"changed continuous fixture\n", None, {
                "gba-link-continuous.gba", "mgba-gba-wifi-link-v9.8.7-android-arm64.zip",
                "RELEASE-PROVENANCE.json", "SHA256SUMS",
            }),
            ("release_notes", b"Changed reviewed notes.\n", None, {
                "mgba-gba-wifi-link-v9.8.7-android-arm64.zip", "RELEASE-PROVENANCE.json", "SHA256SUMS",
            }),
            ("metadata", b"", None, {
                "mgba-gba-wifi-link-v9.8.7-android-arm64.zip", "RELEASE-PROVENANCE.json", "SHA256SUMS",
            }),
            ("install_template", b"\nChanged install text.\n", "VERIFY_STALE", set()),
            ("source_template", b"\nChanged source text.\n", "VERIFY_STALE", set()),
            ("licence", b"substituted licence\n", "PACKAGE_LICENCE", set()),
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline_inputs = self.make_inputs(root)
            build_release(context(), baseline_inputs, root / "baseline")
            baseline_assets = {path.name: path.read_bytes() for path in (root / "baseline").iterdir()}
            for field, suffix, rejection, changed in cases:
                with self.subTest(field=field), tempfile.TemporaryDirectory(dir=root) as case_directory:
                    case_root = Path(case_directory)
                    inputs = self.make_inputs(case_root)
                    changed_context = context()
                    if field == "release_notes":
                        inputs = replace(inputs, release_notes=suffix)
                        changed_context = replace(changed_context, notes_sha256=hashlib.sha256(suffix).hexdigest())
                    elif field == "metadata":
                        changed_build = replace(
                            changed_context.build,
                            actual_builds=tuple(
                                replace(build, source_date_epoch=1_700_000_002)
                                for build in changed_context.build.actual_builds
                            ),
                        )
                        changed_context = replace(
                            changed_context, source_date_epoch=1_700_000_002,
                            build=changed_build,
                        )
                    else:
                        path = getattr(inputs, field)
                        path.write_bytes(path.read_bytes() + suffix)
                        if field == "core":
                            core = ReleaseAsset(
                                "mgba_libretro_android.so", path.stat().st_size,
                                hashlib.sha256(path.read_bytes()).hexdigest(),
                            )
                            changed_context = replace(
                                changed_context,
                                build=replace(
                                    changed_context.build,
                                    actual_builds=tuple(
                                        replace(build, core=core)
                                        for build in changed_context.build.actual_builds
                                    ),
                                ),
                            )
                    if rejection == "PACKAGE_LICENCE":
                        with self.assertRaisesRegex(PackageError, "^PACKAGE_LICENCE$"):
                            build_release(changed_context, inputs, case_root / "release")
                    elif rejection == "VERIFY_STALE":
                        with self.assertRaisesRegex(VerificationError, "^VERIFY_STALE$"):
                            build_release(changed_context, inputs, case_root / "release")
                    else:
                        build_release(changed_context, inputs, case_root / "release")
                        actual = {path.name: path.read_bytes() for path in (case_root / "release").iterdir()}
                        self.assertEqual({name for name in actual if actual[name] != baseline_assets[name]}, changed)
