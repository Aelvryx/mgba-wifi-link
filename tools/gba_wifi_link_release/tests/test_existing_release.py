"""Read-only verification of retained first-run release evidence."""

from dataclasses import replace
from contextlib import redirect_stdout
import hashlib
from io import StringIO
import json
from pathlib import Path
import tempfile
import unittest

from tools.gba_wifi_link_release.existing_release import (
    ExistingReleaseError,
    ExistingReleaseStatus,
    verify_existing_public_release,
)
from tools.gba_wifi_link_release.github import (
    AttestationSubject,
    CANONICAL_SIGNER_WORKFLOW,
    RemoteAsset,
    RemoteRelease,
)
from tools.gba_wifi_link_release.packager import build_release
from tools.gba_wifi_link_release.render import render_release_body
import tools.gba_wifi_link_release.tests.test_packager as package_tests


NOTES = b"Reviewed synthetic release notes.\n"


class ReadOnlyClient:
    """Substitute only GitHub reads; every mutating operation is forbidden."""

    def __init__(self, release: RemoteRelease | None, files: dict[str, bytes]):
        self.release = release
        self.files = files
        self.calls: list[tuple[object, ...]] = []
        self.attestation_failure = False

    def get_release(self, tag: str) -> RemoteRelease | None:
        self.calls.append(("get", tag))
        return self.release

    def download_assets(self, release_id: int, output: Path) -> None:
        self.calls.append(("download", release_id))
        for name, data in self.files.items():
            path = output / name
            path.write_bytes(data)
            path.chmod(0o644)

    def verify_attestations(
        self,
        subjects: tuple[AttestationSubject, ...],
        *,
        source_digest: str,
        signer_workflow: str,
    ) -> None:
        self.calls.append((
            "verify-attestations",
            tuple(subject.name for subject in subjects),
            source_digest,
            signer_workflow,
        ))
        if self.attestation_failure:
            raise ValueError("NO_ATTESTATION")
        self.asserted_subjects = tuple(
            (subject.name, subject.size, subject.sha256,
             hashlib.sha256(subject.path.read_bytes()).hexdigest())
            for subject in subjects
        )

    def create_draft(self, *args, **kwargs):
        raise AssertionError("existing-release verification must be read-only")

    upload = create_draft
    publish = create_draft
    delete_draft = create_draft


class ExistingReleaseTest(unittest.TestCase):
    def make_attempt(self, root: Path, *, identity_offset: int = 0):
        root.mkdir(parents=True, exist_ok=True)
        context = package_tests.context()
        gates = tuple(
            replace(gate, run_id=gate.run_id + identity_offset,
                    job_id=gate.job_id + identity_offset)
            for gate in context.gates
        )
        assert context.build is not None
        actual_builds = tuple(
            replace(build, run_id=build.run_id + identity_offset,
                    job_id=build.job_id + identity_offset)
            for build in context.build.actual_builds
        )
        context = replace(
            context,
            gates=gates,
            build=replace(context.build, actual_builds=actual_builds),
        )
        output = root / f"attempt-{identity_offset}"
        build_release(context, package_tests.PackageTest().make_inputs(root), output)
        return context, output

    @staticmethod
    def remote_for(context, output: Path, body: bytes) -> tuple[RemoteRelease, dict[str, bytes]]:
        files = {path.name: path.read_bytes() for path in output.iterdir()}
        assets = tuple(
            RemoteAsset(path.name, path.stat().st_size,
                        hashlib.sha256(path.read_bytes()).hexdigest(), index + 10)
            for index, path in enumerate(output.iterdir())
        )
        return (
            RemoteRelease(7, context.tag, context.commit, body, False,
                          context.prerelease, assets),
            files,
        )

    def test_absent_release_returns_proceed_without_mutation(self):
        client = ReadOnlyClient(None, {})
        context = package_tests.context()

        result = verify_existing_public_release(client, context, NOTES)

        self.assertEqual(result.status, ExistingReleaseStatus.PROCEED)
        self.assertIsNone(result.release_id)
        self.assertEqual(client.calls, [("get", "v9.8.7")])

    def test_second_attempt_accepts_retained_first_run_before_rebuilding(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, output = self.make_attempt(root / "first")
            second = replace(first)
            second_gates = tuple(
                replace(gate, run_id=gate.run_id + 10_000,
                        job_id=gate.job_id + 20_000)
                for gate in first.gates
            )
            assert first.build is not None
            second_builds = tuple(
                replace(build, run_id=build.run_id + 30_000,
                        job_id=build.job_id + 40_000)
                for build in first.build.actual_builds
            )
            second = replace(
                second,
                gates=second_gates,
                build=replace(first.build, actual_builds=second_builds),
            )
            body = render_release_body(first, NOTES.decode("utf-8"))
            remote, files = self.remote_for(first, output, body)
            client = ReadOnlyClient(remote, files)

            result = verify_existing_public_release(client, second, NOTES)

            self.assertEqual(result.status, ExistingReleaseStatus.REUSED)
            self.assertEqual(result.release_id, 7)
            self.assertEqual(result.retained_context, first)
            self.assertEqual(client.calls, [
                ("get", "v9.8.7"),
                ("download", 7),
                ("verify-attestations", (
                    "mgba_libretro_android.so",
                    "mgba-gba-wifi-link-v9.8.7-android-arm64.zip",
                ), first.commit, CANONICAL_SIGNER_WORKFLOW),
            ])
            self.assertTrue(all(expected == observed
                                for _, _, expected, observed in client.asserted_subjects))

    def test_draft_and_remote_metadata_conflicts_fail_without_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            context, output = self.make_attempt(root)
            body = render_release_body(context, NOTES.decode("utf-8"))
            remote, files = self.remote_for(context, output, body)
            cases = {
                "draft": replace(remote, draft=True),
                "target": replace(remote, target="c" * 40),
                "body": replace(remote, body=b"changed human body\n"),
                "classification": replace(remote, prerelease=False),
                "missing": replace(remote, assets=remote.assets[:-1]),
                "size": replace(remote, assets=(replace(remote.assets[0], size=1), *remote.assets[1:])),
                "digest": replace(remote, assets=(replace(remote.assets[0], sha256="0" * 64), *remote.assets[1:])),
            }
            for name, conflicting in cases.items():
                with self.subTest(name=name):
                    client = ReadOnlyClient(conflicting, files)
                    with self.assertRaisesRegex(ExistingReleaseError, "^EXISTING_RELEASE_CONFLICT$"):
                        verify_existing_public_release(client, context, NOTES)
                    self.assertFalse(any(call[0] in {"create", "upload", "publish", "delete"}
                                         for call in client.calls))

    def test_malformed_or_missing_downloaded_evidence_fails_without_attestation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            context, output = self.make_attempt(root)
            body = render_release_body(context, NOTES.decode("utf-8"))
            remote, files = self.remote_for(context, output, body)
            cases = {
                "missing": {key: value for key, value in files.items()
                            if key != "RELEASE-PROVENANCE.json"},
                "malformed": {**files, "RELEASE-PROVENANCE.json": b"{}\n"},
                "checksum": {**files, "gba-link-test.gba": b"corrupt"},
            }
            for name, downloaded in cases.items():
                with self.subTest(name=name):
                    client = ReadOnlyClient(remote, downloaded)
                    with self.assertRaisesRegex(ExistingReleaseError, "^EXISTING_RELEASE_CONFLICT$"):
                        verify_existing_public_release(client, context, NOTES)
                    self.assertFalse(any(call[0] == "verify-attestations" for call in client.calls))

    def test_coherent_release_for_a_different_source_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = package_tests.context()
            different = replace(expected, tag_object="c" * 40)
            output = root / "different"
            build_release(different, package_tests.PackageTest().make_inputs(root), output)
            body = render_release_body(different, NOTES.decode("utf-8"))
            remote, files = self.remote_for(different, output, body)
            client = ReadOnlyClient(remote, files)

            with self.assertRaisesRegex(ExistingReleaseError, "^EXISTING_RELEASE_CONFLICT$"):
                verify_existing_public_release(client, expected, NOTES)

            self.assertFalse(any(call[0] == "verify-attestations" for call in client.calls))

    def test_missing_exact_attestation_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            context, output = self.make_attempt(root)
            body = render_release_body(context, NOTES.decode("utf-8"))
            remote, files = self.remote_for(context, output, body)
            client = ReadOnlyClient(remote, files)
            client.attestation_failure = True

            with self.assertRaisesRegex(ExistingReleaseError, "^EXISTING_RELEASE_CONFLICT$"):
                verify_existing_public_release(client, context, NOTES)

            self.assertEqual(client.calls[-1][0], "verify-attestations")

    def test_cli_reports_absent_release_as_proceed_without_building(self):
        from tools.gba_wifi_link_release.cli import _context_dict, main

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            context = package_tests.context()
            context_path = root / "context.json"
            context_path.write_text(json.dumps(_context_dict(context)), encoding="utf-8")
            notes_path = root / "notes.md"
            notes_path.write_bytes(NOTES)
            state_path = root / "state.json"
            state_path.write_text(json.dumps({"calls": [], "release": None}), encoding="utf-8")
            environment = __import__("os").environ.copy()
            __import__("os").environ["GBA_WIFI_LINK_FAKE_GH_STATE"] = str(state_path)
            output = StringIO()
            try:
                with redirect_stdout(output):
                    status = main([
                        "verify-existing", "--context", str(context_path),
                        "--notes", str(notes_path), "--repository",
                        "Aelvryx/mgba-wifi-link", "--test-mode", "--gh-bin",
                        str(Path(__file__).with_name("fake_gh.py")),
                    ])
            finally:
                __import__("os").environ.clear()
                __import__("os").environ.update(environment)
            self.assertEqual(status, 0)
            self.assertEqual(output.getvalue(), '{"release_id":null,"status":"proceed"}\n')


if __name__ == "__main__":
    unittest.main()
