"""Transactional release-publication behavior at a fake GitHub boundary."""

from dataclasses import replace
import base64
from contextlib import redirect_stderr, redirect_stdout
import hashlib
from io import StringIO
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.gba_wifi_link_release.github import (
    AttestationSubject,
    GhClient,
    GitHubError,
    RemoteAsset,
    RemoteRelease,
)
from tools.gba_wifi_link_release.packager import build_release
from tools.gba_wifi_link_release.publisher import (
    PublishError,
    ReleaseConflict,
    publish_release,
)
import tools.gba_wifi_link_release.tests.test_packager as package_tests
from tools.gba_wifi_link_release.verifier import VerificationError, verify_release


ROOT = Path(__file__).resolve().parents[3]
FAKE_GH = Path(__file__).with_name("fake_gh.py")
BODY = b"# Reviewed synthetic release\n"


class FakeClient:
    """In-memory protocol fake: GitHub is the only substituted boundary."""

    def __init__(self):
        self.calls: list[tuple[object, ...]] = []
        self.release: RemoteRelease | None = None
        self.files: dict[str, bytes] = {}
        self.fail_create = False
        self.fail_upload_at: int | None = None
        self.fail_attestation = False
        self.on_verify_attestations = None
        self.verified_subjects: tuple[tuple[str, Path, bytes, str], ...] = ()
        self.publish_error: Exception | None = None
        self.publish_before_error = False
        self.after_upload = None
        self.foreign_drafts = {999}

    def get_release(self, tag: str) -> RemoteRelease | None:
        self.calls.append(("get", tag))
        if self.release is not None and self.release.tag == tag:
            return self.release
        return None

    def create_draft(self, context, body: bytes) -> RemoteRelease:
        self.calls.append(("create", context.tag, context.commit, body))
        if self.fail_create:
            raise OSError("CREATE")
        self.release = RemoteRelease(100, context.tag, context.commit, body, True,
                                     context.prerelease, ())
        return self.release

    def upload(self, release_id: int, path: Path) -> RemoteAsset:
        self.calls.append(("upload", release_id, path.name))
        assert self.release is not None
        if self.fail_upload_at == len(self.release.assets):
            raise OSError("UPLOAD")
        data = path.read_bytes()
        asset = RemoteAsset(path.name, len(data), hashlib.sha256(data).hexdigest(),
                            10 + len(self.release.assets))
        self.files[path.name] = data
        self.release = replace(self.release, assets=(*self.release.assets, asset))
        if len(self.release.assets) == 7 and self.after_upload is not None:
            self.after_upload(self)
        return asset

    def download_assets(self, release_id: int, output: Path) -> None:
        self.calls.append(("download", release_id))
        assert self.release is not None and self.release.id == release_id
        for name, data in self.files.items():
            (output / name).write_bytes(data)

    def verify_attestations(self, subjects: tuple[AttestationSubject, ...], **identity) -> None:
        self.calls.append(("verify-attestations", *(subject.name for subject in subjects)))
        if self.on_verify_attestations is not None:
            self.on_verify_attestations(subjects)
        self.verified_subjects = tuple(
            (subject.name, subject.path, subject.path.read_bytes(), subject.sha256)
            for subject in subjects
        )
        if self.fail_attestation or any(
            len(data) != subject.size or hashlib.sha256(data).hexdigest() != subject.sha256
            for subject, (_, _, data, _) in zip(subjects, self.verified_subjects)
        ):
            raise OSError("ATTEST")

    def publish(self, release_id: int) -> None:
        self.calls.append(("publish", release_id))
        assert self.release is not None and self.release.id == release_id
        if self.publish_before_error:
            self.release = replace(self.release, draft=False)
        if self.publish_error is not None:
            raise self.publish_error
        self.release = replace(self.release, draft=False)

    def delete_draft(self, release_id: int) -> None:
        self.calls.append(("delete", release_id))
        assert self.release is not None and self.release.id == release_id
        assert self.release.draft
        self.release = None


class PublisherTest(unittest.TestCase):
    def make_release(self, root: Path):
        maker = package_tests.PackageTest()
        context = package_tests.context()
        output = root / "release"
        build_release(context, maker.make_inputs(root), output)
        return verify_release(output, context)

    @staticmethod
    def exact_remote(release_set, body: bytes, *, release_id: int = 5,
                     draft: bool = False) -> tuple[RemoteRelease, dict[str, bytes]]:
        assets = tuple(RemoteAsset(asset.name, asset.size, asset.sha256, 20 + index)
                       for index, asset in enumerate(release_set.assets))
        files = {asset.name: (release_set.directory / asset.name).read_bytes()
                 for asset in release_set.assets}
        context = release_set.context
        return (RemoteRelease(release_id, context.tag, context.commit, body, draft,
                              context.prerelease, assets), files)

    def test_success_stages_verifies_attests_and_publishes_the_exact_set(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            client = FakeClient()

            result = publish_release(client, release_set, BODY)

            self.assertTrue(result.published)
            self.assertFalse(result.reused)
            self.assertEqual(client.calls, [
                ("get", "v9.8.7"),
                ("verify-attestations", "mgba_libretro_android.so",
                 "mgba-gba-wifi-link-v9.8.7-android-arm64.zip"),
                ("create", "v9.8.7", "b" * 40, BODY),
                ("upload", 100, "mgba_libretro_android.so"),
                ("upload", 100, "gba-link-test.gba"),
                ("upload", 100, "gba-link-continuous.gba"),
                ("upload", 100, "INSTALL-AND-USAGE.md"),
                ("upload", 100, "mgba-gba-wifi-link-v9.8.7-android-arm64.zip"),
                ("upload", 100, "RELEASE-PROVENANCE.json"),
                ("upload", 100, "SHA256SUMS"),
                ("get", "v9.8.7"),
                ("download", 100),
                ("publish", 100),
                ("get", "v9.8.7"),
                ("download", 100),
            ])

    def test_local_reverification_happens_before_any_github_operation(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            (release_set.directory / "gba-link-test.gba").write_bytes(b"substituted")
            client = FakeClient()

            with self.assertRaisesRegex(VerificationError, "^VERIFY_"):
                publish_release(client, release_set, BODY)

            self.assertEqual(client.calls, [])

    def test_each_failed_upload_removes_only_the_owned_private_draft(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            for index in range(7):
                with self.subTest(index=index):
                    client = FakeClient()
                    client.fail_upload_at = index

                    with self.assertRaisesRegex(OSError, "^UPLOAD$"):
                        publish_release(client, release_set, BODY)

                    self.assertEqual(client.calls[-2:], [("get", "v9.8.7"), ("delete", 100)])
                    self.assertIsNone(client.release)
                    self.assertEqual(client.foreign_drafts, {999})

    def test_draft_creation_failure_never_attempts_cleanup_or_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            client = FakeClient()
            client.fail_create = True

            with self.assertRaisesRegex(OSError, "^CREATE$"):
                publish_release(client, release_set, BODY)

            self.assertEqual(client.calls, [("get", "v9.8.7"),
                                            ("verify-attestations", "mgba_libretro_android.so",
                                             "mgba-gba-wifi-link-v9.8.7-android-arm64.zip"),
                                            ("create", "v9.8.7", "b" * 40, BODY)])

    def test_draft_readback_mismatches_cleanup_the_owned_draft(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            cases = {
                "body": lambda client: setattr(client, "release", replace(client.release, body=b"wrong\n")),
                "count": lambda client: setattr(client, "release", replace(client.release, assets=client.release.assets[:-1])),
                "size": lambda client: setattr(client, "release", replace(
                    client.release, assets=(replace(client.release.assets[0], size=1), *client.release.assets[1:]))),
                "hash": lambda client: setattr(client, "release", replace(
                    client.release, assets=(replace(client.release.assets[0], sha256="0" * 64), *client.release.assets[1:]))),
                "bytes": lambda client: client.files.__setitem__("gba-link-test.gba", b"wrong"),
            }
            for name, mutation in cases.items():
                with self.subTest(name=name):
                    client = FakeClient()
                    client.after_upload = mutation

                    with self.assertRaisesRegex(PublishError, "^PUBLISH_"):
                        publish_release(client, release_set, BODY)

                    self.assertEqual(client.calls[-2:], [("get", "v9.8.7"), ("delete", 100)])
                    self.assertIsNone(client.release)

    def test_attestation_failure_prevents_every_release_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            client = FakeClient()
            client.fail_attestation = True

            with self.assertRaisesRegex(OSError, "^ATTEST$"):
                publish_release(client, release_set, BODY)

            self.assertEqual(client.calls, [
                ("get", "v9.8.7"),
                ("verify-attestations", "mgba_libretro_android.so",
                 "mgba-gba-wifi-link-v9.8.7-android-arm64.zip"),
            ])
            self.assertIsNone(client.release)

    def test_attestation_verifies_immutable_subject_snapshots_despite_original_swap(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            client = FakeClient()
            expected = {
                asset.name: (release_set.directory / asset.name).read_bytes()
                for asset in release_set.assets
                if asset.name == "mgba_libretro_android.so" or asset.name.endswith(".zip")
            }

            def swap_originals(subjects):
                self.assertTrue(all(subject.path.parent != release_set.directory for subject in subjects))
                (release_set.directory / "mgba_libretro_android.so").write_bytes(b"substituted")
                (release_set.directory / "mgba-gba-wifi-link-v9.8.7-android-arm64.zip").write_bytes(b"substituted")

            client.on_verify_attestations = swap_originals
            result = publish_release(client, release_set, BODY)

            self.assertTrue(result.published)
            self.assertEqual({name: data for name, _, data, _ in client.verified_subjects}, expected)
            self.assertTrue(all(hashlib.sha256(data).hexdigest() == digest
                                for _, _, data, digest in client.verified_subjects))

    def test_untrusted_created_draft_is_not_deleted(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            client = FakeClient()
            original_create = client.create_draft

            def create_wrong_target(context, body):
                created = original_create(context, body)
                client.release = replace(created, target="c" * 40)
                return client.release

            client.create_draft = create_wrong_target
            with self.assertRaisesRegex(PublishError, "^PUBLISH_METADATA$"):
                publish_release(client, release_set, BODY)

            self.assertNotIn(("delete", 100), client.calls)
            self.assertIsNotNone(client.release)

    def test_existing_same_tag_draft_is_a_conflict_without_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            client = FakeClient()
            client.release, client.files = self.exact_remote(release_set, BODY, draft=True)

            with self.assertRaisesRegex(ReleaseConflict, "^RELEASE_CONFLICT$"):
                publish_release(client, release_set, BODY)

            self.assertEqual(client.calls, [("get", "v9.8.7")])

    def test_exact_public_rerun_is_read_only(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            client = FakeClient()
            client.release, client.files = self.exact_remote(release_set, BODY)

            result = publish_release(client, release_set, BODY)

            self.assertTrue(result.published)
            self.assertTrue(result.reused)
            self.assertEqual(client.calls, [
                ("get", "v9.8.7"),
                ("download", 5),
                ("verify-attestations", "mgba_libretro_android.so",
                 "mgba-gba-wifi-link-v9.8.7-android-arm64.zip"),
            ])

    def test_existing_public_conflicts_are_never_mutated(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            cases = {
                "target": lambda release: replace(release, target="c" * 40),
                "body": lambda release: replace(release, body=b"wrong\n"),
                "classification": lambda release: replace(release, prerelease=False),
                "count": lambda release: replace(release, assets=release.assets[:-1]),
                "digest": lambda release: replace(
                    release, assets=(replace(release.assets[0], sha256="0" * 64), *release.assets[1:])),
            }
            for name, mutation in cases.items():
                with self.subTest(name=name):
                    client = FakeClient()
                    remote, files = self.exact_remote(release_set, BODY)
                    client.release, client.files = mutation(remote), files

                    with self.assertRaisesRegex(ReleaseConflict, "^RELEASE_CONFLICT$"):
                        publish_release(client, release_set, BODY)

                    mutations = {"create", "upload", "delete", "attest", "publish"}
                    self.assertFalse(any(call[0] in mutations for call in client.calls))

    def test_ambiguous_publish_succeeds_only_after_exact_public_readback(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            client = FakeClient()
            client.publish_error = OSError("timeout")
            client.publish_before_error = True

            result = publish_release(client, release_set, BODY)

            self.assertTrue(result.published)
            self.assertFalse(result.reused)
            self.assertEqual(client.calls[-3:], [("publish", 100), ("get", "v9.8.7"),
                                                 ("download", 100)])

    def test_ambiguous_publish_conflict_is_never_deleted_or_retried(self):
        with tempfile.TemporaryDirectory() as directory:
            release_set = self.make_release(Path(directory))
            client = FakeClient()
            client.publish_error = OSError("timeout")
            client.publish_before_error = True

            def corrupt_public(fake):
                if fake.release is not None and not fake.release.draft:
                    fake.release = replace(fake.release, body=b"wrong\n")

            original_get = client.get_release

            def get_after_publish(tag):
                result = original_get(tag)
                corrupt_public(client)
                return client.release

            client.get_release = get_after_publish
            with self.assertRaisesRegex(ReleaseConflict, "^RELEASE_CONFLICT$"):
                publish_release(client, release_set, BODY)

            self.assertNotIn(("delete", 100), client.calls)
            self.assertEqual(sum(call[0] == "publish" for call in client.calls), 1)

    def test_cli_allows_fake_executable_only_in_test_mode(self):
        from tools.gba_wifi_link_release.cli import _context_dict, main
        from tools.gba_wifi_link_release.render import render_release_body

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            release_set = self.make_release(root)
            body_path = root / "body.md"
            body_path.write_bytes(render_release_body(release_set.context,
                                                      "Reviewed synthetic release notes.\n"))
            context_path = root / "context.json"
            context_path.write_text(json.dumps(_context_dict(release_set.context)), encoding="utf-8")
            state_path = root / "gh-state.json"
            state_path.write_text(json.dumps({"calls": [], "release": None}), encoding="utf-8")
            environment = {"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)}
            old = __import__("os").environ.copy()
            __import__("os").environ.update(environment)
            try:
                published = StringIO()
                with patch("tools.gba_wifi_link_release.cli.verify_remote_tag") as remote_check:
                    with redirect_stdout(published):
                        self.assertEqual(main([
                            "publish", "--context", str(context_path), "--output", str(release_set.directory),
                            "--body", str(body_path), "--repository", "Aelvryx/mgba-wifi-link",
                            "--test-mode", "--gh-bin", str(FAKE_GH),
                        ]), 0)
                    remote_check.assert_called_once_with(
                        release_set.context, "Aelvryx/mgba-wifi-link"
                    )
                self.assertEqual(published.getvalue(), '{"release_id":1,"reused":false}\n')
                with redirect_stderr(StringIO()):
                    self.assertNotEqual(main([
                        "publish", "--context", str(context_path), "--output", str(release_set.directory),
                        "--body", str(body_path), "--repository", "Aelvryx/mgba-wifi-link",
                        "--gh-bin", str(FAKE_GH),
                    ]), 0)
            finally:
                __import__("os").environ.clear()
                __import__("os").environ.update(old)
            state = json.loads(state_path.read_text(encoding="utf-8"))
            self.assertFalse(state["release"]["draft"])
            api_calls = [call for call in state["calls"] if call[:1] == ["api"]]
            self.assertTrue(api_calls)
            self.assertTrue(all(
                "--repo" not in call and "--output" not in call
                for call in api_calls
            ))
            self.assertTrue(all(any(
                argument.startswith("repos/Aelvryx/mgba-wifi-link/")
                for argument in call
            ) for call in api_calls))
            attestation_calls = [call for call in state["calls"]
                                 if call[:2] == ["attestation", "verify"]]
            self.assertTrue(all(call[-2:] == ["--repo", "Aelvryx/mgba-wifi-link"]
                                for call in attestation_calls))
            self.assertTrue(any(
                "/releases/1/assets?name=mgba_libretro_android.so" in argument
                for call in state["calls"] for argument in call
            ))

    def test_cli_github_smoke_uses_one_read_only_release_get(self):
        from tools.gba_wifi_link_release.cli import main

        with tempfile.TemporaryDirectory() as directory:
            state_path = Path(directory) / "gh-state.json"
            state_path.write_text(json.dumps({
                "calls": [],
                "release": {
                    "assets": [], "body": "Synthetic public release.\n", "draft": False,
                    "id": 7, "prerelease": True, "tag_name": "v0.2.0",
                    "target_commitish": "a" * 40,
                },
            }), encoding="utf-8")
            old = __import__("os").environ.copy()
            __import__("os").environ["GBA_WIFI_LINK_FAKE_GH_STATE"] = str(state_path)
            try:
                output = StringIO()
                with redirect_stdout(output):
                    self.assertEqual(main([
                        "github-smoke", "--repository", "Aelvryx/mgba-wifi-link",
                        "--tag", "v0.2.0", "--test-mode", "--gh-bin", str(FAKE_GH),
                    ]), 0)
            finally:
                __import__("os").environ.clear()
                __import__("os").environ.update(old)
            self.assertEqual(output.getvalue(),
                             '{"draft":false,"id":7,"prerelease":true,"tag":"v0.2.0"}\n')
            state = json.loads(state_path.read_text(encoding="utf-8"))
            self.assertEqual(state["calls"], [[
                "api", "repos/Aelvryx/mgba-wifi-link/releases/tags/v0.2.0",
            ]])

    def test_gh_client_delete_uses_parser_faithful_rest_arguments(self):
        with tempfile.TemporaryDirectory() as directory:
            state_path = Path(directory) / "gh-state.json"
            state_path.write_text(json.dumps({
                "calls": [],
                "release": {
                    "assets": [], "body": "Synthetic private draft.\n", "draft": True,
                    "id": 7, "prerelease": True, "tag_name": "v9.8.7",
                    "target_commitish": "a" * 40,
                },
            }), encoding="utf-8")
            client = GhClient(
                "Aelvryx/mgba-wifi-link", gh=str(FAKE_GH),
                env={"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)},
            )

            client.delete_draft(7)

            state = json.loads(state_path.read_text(encoding="utf-8"))
            self.assertEqual(state["calls"], [[
                "api", "--method", "DELETE", "repos/Aelvryx/mgba-wifi-link/releases/7",
            ]])

    def test_parser_faithful_fake_rejects_equals_forms_for_unsupported_api_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state_path = root / "gh-state.json"
            release = {
                "assets": [], "body": "Synthetic public release.\n", "draft": False,
                "id": 7, "prerelease": True, "tag_name": "v0.2.0",
                "target_commitish": "a" * 40,
            }
            for index, option in enumerate((
                "--repo=Aelvryx/mgba-wifi-link", "--output=" + str(root / "asset.bin"),
            )):
                with self.subTest(option=option):
                    state_path.write_text(json.dumps({"calls": [], "release": release}),
                                          encoding="utf-8")
                    completed = subprocess.run(
                        [str(FAKE_GH), "api",
                         "repos/Aelvryx/mgba-wifi-link/releases/tags/v0.2.0", option],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        env={"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)}, check=False,
                    )
                    self.assertEqual(completed.returncode, 2)
                    self.assertIn(b"unknown flag for gh api", completed.stderr)
                    self.assertEqual(json.loads(state_path.read_text(encoding="utf-8"))["calls"], [[
                        "api", "repos/Aelvryx/mgba-wifi-link/releases/tags/v0.2.0", option,
                    ]])

    def test_gh_client_rejects_and_removes_a_mismatched_streamed_asset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = b"expected asset\n"
            state_path = root / "gh-state.json"
            state_path.write_text(json.dumps({
                "calls": [],
                "files": {"SHA256SUMS": base64.b64encode(b"substituted asset\n").decode("ascii")},
                "release": {
                    "assets": [{
                        "digest": "sha256:" + hashlib.sha256(expected).hexdigest(),
                        "id": 10, "name": "SHA256SUMS", "size": len(expected),
                    }],
                    "body": "Synthetic private draft.\n", "draft": True, "id": 7,
                    "prerelease": True, "tag_name": "v9.8.7",
                    "target_commitish": "a" * 40,
                },
            }), encoding="utf-8")
            client = GhClient(
                "Aelvryx/mgba-wifi-link", gh=str(FAKE_GH),
                env={"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)},
            )
            output = root / "download"
            output.mkdir()

            with self.assertRaisesRegex(GitHubError, "^GITHUB_DOWNLOAD$"):
                client.download_assets(7, output)

            self.assertEqual(tuple(output.iterdir()), ())
            calls = json.loads(state_path.read_text(encoding="utf-8"))["calls"]
            self.assertEqual(calls, [
                ["api", "repos/Aelvryx/mgba-wifi-link/releases/7/assets"],
                ["api", "repos/Aelvryx/mgba-wifi-link/releases/assets/10",
                 "--header", "Accept: application/octet-stream"],
            ])

    def test_gh_client_rejects_oversized_metadata_before_asset_download(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state_path = root / "gh-state.json"
            state_path.write_text(json.dumps({
                "calls": [], "files": {},
                "release": {
                    "assets": [{
                        "digest": "sha256:" + "0" * 64,
                        "id": 10, "name": "mgba_libretro_android.so",
                        "size": 67_108_865,
                    }],
                    "body": "Synthetic release.\n", "draft": False, "id": 7,
                    "prerelease": True, "tag_name": "v9.8.7",
                    "target_commitish": "a" * 40,
                },
            }), encoding="utf-8")
            client = GhClient(
                "Aelvryx/mgba-wifi-link", gh=str(FAKE_GH),
                env={"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)},
            )
            output = root / "download"
            output.mkdir()

            with self.assertRaisesRegex(GitHubError, "^GITHUB_DOWNLOAD$"):
                client.download_assets(7, output)

            self.assertEqual(tuple(output.iterdir()), ())
            self.assertEqual(json.loads(state_path.read_text(encoding="utf-8"))["calls"], [[
                "api", "repos/Aelvryx/mgba-wifi-link/releases/7/assets",
            ]])

    def test_gh_client_rejects_public_aggregate_metadata_before_download(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state_path = root / "gh-state.json"
            assets = [
                {"digest": "sha256:" + "0" * 64, "id": 10,
                 "name": "mgba_libretro_android.so", "size": 67_108_864},
                {"digest": "sha256:" + "1" * 64, "id": 11,
                 "name": "mgba-gba-wifi-link-v9.8.7-android-arm64.zip",
                 "size": 67_108_864},
                {"digest": "sha256:" + "2" * 64, "id": 12,
                 "name": "SHA256SUMS", "size": 1},
            ]
            state_path.write_text(json.dumps({
                "calls": [], "files": {},
                "release": {
                    "assets": assets, "body": "Synthetic release.\n",
                    "draft": False, "id": 7, "prerelease": True,
                    "tag_name": "v9.8.7", "target_commitish": "a" * 40,
                },
            }), encoding="utf-8")
            client = GhClient(
                "Aelvryx/mgba-wifi-link", gh=str(FAKE_GH),
                env={"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)},
            )
            output = root / "download"
            output.mkdir()

            with self.assertRaisesRegex(GitHubError, "^GITHUB_DOWNLOAD$"):
                client.download_assets(7, output)

            self.assertEqual(tuple(output.iterdir()), ())
            self.assertEqual(json.loads(state_path.read_text(encoding="utf-8"))["calls"], [[
                "api", "repos/Aelvryx/mgba-wifi-link/releases/7/assets",
            ]])

    def test_gh_client_stops_stream_at_contract_ceiling_and_removes_partial_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            streamed = b"x" * (2 * 1_048_576)
            declared = b"x"
            marker = root / "stream-complete"
            state_path = root / "gh-state.json"
            state_path.write_text(json.dumps({
                "calls": [], "stream_complete_marker": str(marker),
                "files": {"SHA256SUMS": base64.b64encode(streamed).decode("ascii")},
                "release": {
                    "assets": [{
                        "digest": "sha256:" + hashlib.sha256(declared).hexdigest(),
                        "id": 10, "name": "SHA256SUMS", "size": len(declared),
                    }],
                    "body": "Synthetic release.\n", "draft": False, "id": 7,
                    "prerelease": True, "tag_name": "v9.8.7",
                    "target_commitish": "a" * 40,
                },
            }), encoding="utf-8")
            client = GhClient(
                "Aelvryx/mgba-wifi-link", gh=str(FAKE_GH),
                env={"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)},
            )
            output = root / "download"
            output.mkdir()

            with self.assertRaisesRegex(GitHubError, "^GITHUB_DOWNLOAD$"):
                client.download_assets(7, output)

            self.assertEqual(tuple(output.iterdir()), ())
            self.assertFalse(marker.exists())

    def test_gh_client_rejects_duplicate_json_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            state_path = Path(directory) / "gh-state.json"
            state_path.write_text(json.dumps({"calls": [], "malformed_json": True}), encoding="utf-8")
            client = GhClient("Aelvryx/mgba-wifi-link", gh=str(FAKE_GH),
                              env={"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)},
                              source_digest="b" * 40)

            with self.assertRaisesRegex(GitHubError, "^GITHUB_JSON_DUPLICATE$"):
                client.get_release("v9.8.7")

    def test_gh_client_verifies_existing_exact_attestations_without_create_command(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            core_path = root / "mgba_libretro_android.so"
            archive_path = root / "mgba-gba-wifi-link-v9.8.7-android-arm64.zip"
            core_path.write_bytes(b"core subject bytes\n")
            archive_path.write_bytes(b"archive subject bytes\n")
            state_path = root / "gh-state.json"
            state_path.write_text(json.dumps({"calls": []}), encoding="utf-8")
            subjects = tuple(
                AttestationSubject(
                    path.name, path, path.stat().st_size,
                    hashlib.sha256(path.read_bytes()).hexdigest(),
                )
                for path in (core_path, archive_path)
            )
            client = GhClient("Aelvryx/mgba-wifi-link", gh=str(FAKE_GH),
                              env={"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)},
                              source_digest="b" * 40)

            client.verify_attestations(subjects)

            calls = json.loads(state_path.read_text(encoding="utf-8"))["calls"]
            verify_calls = [call for call in calls if call[:2] == ["attestation", "verify"]]
            self.assertEqual(len(verify_calls), 2)
            for subject, verify_call in zip(subjects, verify_calls):
                self.assertIn(str(subject.path), verify_call)
                self.assertIn("--format", verify_call)
                self.assertIn("json", verify_call)
                self.assertIn("--limit", verify_call)
                self.assertIn("2", verify_call)
                self.assertIn("--predicate-type", verify_call)
                self.assertIn("--signer-workflow", verify_call)
                self.assertIn(
                    "Aelvryx/mgba-wifi-link/.github/workflows/gba-wifi-link-release.yml",
                    verify_call,
                )
                self.assertIn("--source-digest", verify_call)
                self.assertIn("b" * 40, verify_call)
                self.assertEqual(verify_call[-2:], ["--repo", "Aelvryx/mgba-wifi-link"])
            self.assertTrue(all("create" not in call for call in calls))

    def test_gh_client_rejects_missing_or_invalid_attestation_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            core_path = root / "mgba_libretro_android.so"
            archive_path = root / "mgba-gba-wifi-link-v9.8.7-android-arm64.zip"
            core_path.write_bytes(b"core subject bytes\n")
            archive_path.write_bytes(b"archive subject bytes\n")
            subjects = tuple(
                AttestationSubject(
                    path.name, path, path.stat().st_size,
                    hashlib.sha256(path.read_bytes()).hexdigest(),
                )
                for path in (core_path, archive_path)
            )
            for target in subjects:
                for mode in ("missing", "invalid"):
                    with self.subTest(subject=target.name, mode=mode):
                        state_path = root / f"gh-state-{target.name}-{mode}.json"
                        state_path.write_text(json.dumps({
                            "calls": [], "attestation_mode": {target.name: mode},
                        }), encoding="utf-8")
                        client = GhClient("Aelvryx/mgba-wifi-link", gh=str(FAKE_GH),
                                          env={"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)},
                                          source_digest="b" * 40)

                        with self.assertRaisesRegex(GitHubError, "^GITHUB_ATTEST$"):
                            client.verify_attestations(subjects)

    def test_gh_client_rejects_wrong_signer_workflow_or_source_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = (
                root / "mgba_libretro_android.so",
                root / "mgba-gba-wifi-link-v9.8.7-android-arm64.zip",
            )
            for path in paths:
                path.write_bytes(path.name.encode("utf-8"))
            subjects = tuple(
                AttestationSubject(
                    path.name, path, path.stat().st_size,
                    hashlib.sha256(path.read_bytes()).hexdigest(),
                )
                for path in paths
            )
            expected = {
                "signer_workflow": (
                    "Aelvryx/mgba-wifi-link/.github/workflows/gba-wifi-link-release.yml"
                ),
                "source_digest": "b" * 40,
            }
            for case in ("signer", "source"):
                with self.subTest(case=case):
                    state_path = root / f"gh-state-wrong-{case}.json"
                    state_path.write_text(json.dumps({
                        "attestation_expected": expected, "calls": [],
                    }), encoding="utf-8")
                    client = GhClient(
                        "Aelvryx/mgba-wifi-link", gh=str(FAKE_GH),
                        env={"GBA_WIFI_LINK_FAKE_GH_STATE": str(state_path)},
                        source_digest=("c" * 40 if case == "source" else "b" * 40),
                    )
                    signer_patch = patch(
                        "tools.gba_wifi_link_release.github.CANONICAL_SIGNER_WORKFLOW",
                        "evil/example/.github/workflows/release.yml",
                    ) if case == "signer" else patch(
                        "tools.gba_wifi_link_release.github.CANONICAL_SIGNER_WORKFLOW",
                        expected["signer_workflow"],
                    )
                    with signer_patch, self.assertRaisesRegex(GitHubError, "^GITHUB_ATTEST$"):
                        client.verify_attestations(subjects)


if __name__ == "__main__":
    unittest.main()
