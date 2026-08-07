"""Tests for fail-closed admission of tagged releases."""

from dataclasses import replace
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.gba_wifi_link_release.admission import (
    AdmissionError,
    REQUIRED_GATES,
    admit_release,
    verify_remote_tag,
)


def git(repo: Path, *args: str) -> str:
    return subprocess.run(
        ("git", "-C", str(repo), *args),
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    ).stdout.strip()


def evidence(commit: str) -> dict[str, object]:
    return {
        "repository": "Aelvryx/mgba-wifi-link",
        "source_date_epoch": 1_700_000_000,
        "commit": commit,
        "gates": [
            {
                "name": name,
                "run_id": index + 100,
                "job_id": index + 200,
                "conclusion": "success",
            }
            for index, name in enumerate(REQUIRED_GATES)
        ],
    }


def make_repo(tag: str = "v9.8.7", *, annotated: bool = True,
              notes: str = "Reviewed synthetic release notes.\n") -> Path:
    repo = Path(tempfile.mkdtemp())
    git(repo, "init", "--initial-branch=master")
    git(repo, "config", "user.name", "Release Test")
    git(repo, "config", "user.email", "release-test@example.invalid")
    notes_path = repo / "packaging/gba-wifi-link/releases" / tag / "RELEASE-NOTES.md"
    notes_path.parent.mkdir(parents=True)
    notes_path.write_text(notes, encoding="utf-8")
    (repo / "README").write_text("release test\n", encoding="utf-8")
    git(repo, "add", ".")
    git(repo, "commit", "-m", "release input")
    commit = git(repo, "rev-parse", "HEAD")
    git(repo, "update-ref", "refs/remotes/origin/master", commit)
    if annotated:
        git(repo, "tag", "-a", tag, "-m", f"Release {tag}")
    else:
        git(repo, "tag", tag)
    return repo


class AdmissionTest(unittest.TestCase):
    def addCleanupRepo(self, repo: Path) -> None:
        self.addCleanup(shutil.rmtree, repo)

    def test_annotated_reachable_tag_is_admitted(self):
        repo = make_repo("v9.8.7")
        self.addCleanupRepo(repo)
        commit = git(repo, "rev-parse", "v9.8.7^{commit}")

        context = admit_release(repo, "v9.8.7", evidence(commit))

        self.assertEqual(context.version, "9.8.7")
        self.assertTrue(context.prerelease)
        self.assertNotEqual(context.tag_object, context.commit)
        self.assertEqual(tuple(gate.name for gate in context.gates), REQUIRED_GATES)
        self.assertEqual(
            context.notes_sha256,
            hashlib.sha256(b"Reviewed synthetic release notes.\n").hexdigest(),
        )

    def test_lightweight_tag_is_rejected(self):
        repo = make_repo(annotated=False)
        self.addCleanupRepo(repo)

        with self.assertRaisesRegex(AdmissionError, "TAG_NOT_ANNOTATED"):
            admit_release(repo, "v9.8.7", evidence(git(repo, "rev-parse", "HEAD")))

    def test_noncanonical_tag_is_rejected(self):
        repo = make_repo("release-9.8.7")
        self.addCleanupRepo(repo)

        with self.assertRaisesRegex(AdmissionError, "TAG_FORMAT"):
            admit_release(repo, "release-9.8.7", evidence(git(repo, "rev-parse", "HEAD")))

    def test_tag_commit_outside_origin_master_is_rejected(self):
        repo = make_repo()
        self.addCleanupRepo(repo)
        git(repo, "checkout", "--detach")
        (repo / "off-master").write_text("off master\n", encoding="utf-8")
        git(repo, "add", "off-master")
        git(repo, "commit", "-m", "off master")
        git(repo, "tag", "-a", "v9.8.8", "-m", "Release v9.8.8")
        commit = git(repo, "rev-parse", "v9.8.8^{commit}")

        with self.assertRaisesRegex(AdmissionError, "TAG_NOT_PROTECTED"):
            admit_release(repo, "v9.8.8", evidence(commit))

    def test_missing_tracked_notes_are_rejected(self):
        repo = make_repo()
        self.addCleanupRepo(repo)
        notes = repo / "packaging/gba-wifi-link/releases/v9.8.7/RELEASE-NOTES.md"
        notes.unlink()
        commit = git(repo, "rev-parse", "v9.8.7^{commit}")

        with self.assertRaisesRegex(AdmissionError, "NOTES_MISSING"):
            admit_release(repo, "v9.8.7", evidence(commit))

    def test_placeholder_or_generated_identity_in_notes_is_rejected(self):
        repo = make_repo(notes="TBD\n")
        self.addCleanupRepo(repo)
        commit = git(repo, "rev-parse", "v9.8.7^{commit}")

        with self.assertRaisesRegex(AdmissionError, "NOTES_PLACEHOLDER"):
            admit_release(repo, "v9.8.7", evidence(commit))

    def test_conflicting_version_text_is_rejected(self):
        repo = make_repo(notes="This release is v9.8.6.\n")
        self.addCleanupRepo(repo)
        commit = git(repo, "rev-parse", "v9.8.7^{commit}")

        with self.assertRaisesRegex(AdmissionError, "NOTES_VERSION"):
            admit_release(repo, "v9.8.7", evidence(commit))

    def test_unsuccessful_or_wrong_commit_gate_is_rejected(self):
        repo = make_repo()
        self.addCleanupRepo(repo)
        commit = git(repo, "rev-parse", "v9.8.7^{commit}")
        invalid = evidence(commit)
        invalid["gates"][0]["conclusion"] = "failure"  # type: ignore[index]

        with self.assertRaisesRegex(AdmissionError, "GATE_CONCLUSION"):
            admit_release(repo, "v9.8.7", invalid)

        invalid = evidence("c" * 40)
        with self.assertRaisesRegex(AdmissionError, "EVIDENCE_COMMIT"):
            admit_release(repo, "v9.8.7", invalid)

    def test_remote_recheck_rejects_tag_object_commit_substitution(self):
        repo = make_repo()
        self.addCleanupRepo(repo)
        commit = git(repo, "rev-parse", "v9.8.7^{commit}")
        context = admit_release(repo, "v9.8.7", evidence(commit))

        verify_remote_tag(context, str(repo))
        with self.assertRaisesRegex(AdmissionError, "REMOTE_TAG_OBJECT"):
            verify_remote_tag(replace(context, tag_object=context.commit), str(repo))
