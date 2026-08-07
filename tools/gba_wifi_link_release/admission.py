"""Fail-closed admission checks for annotated GBA Wi-Fi Link release tags."""

from collections.abc import Mapping, Sequence
import hashlib
from pathlib import Path
import re
import subprocess

from .model import GateResult, ReleaseContext


TAG_RE = re.compile(r"^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
CANONICAL_REPOSITORY = "Aelvryx/mgba-wifi-link"
REQUIRED_WORKFLOW = "GBA Wi-Fi Link"
REQUIRED_GATES = (
    "Complete normal mGBA suite",
    "Focused tests (normal)",
    "Focused tests (ASan + UBSan)",
    "Focused tests (TSan)",
    "Fixture reproducibility",
    "Android arm64 libretro build",
)
_PLACEHOLDER_RE = re.compile(r"\b(?:TBD|TODO)\b|<[^>\n]+>")
_VERSION_RE = re.compile(r"\bv(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\b")
_GENERATED_FIELD_RE = re.compile(
    r"(?im)^\s*(?:repository|tag|annotated tag object|peeled commit|"
    r"source date epoch|version|workflow(?: run)?|assets?|checksums?|"
    r"release provenance|provenance|compatibility|qualification)\s*:"
)
_GENERATED_HEADING_RE = re.compile(r"(?im)^\s*#+\s*(?:compatibility|qualification)\s*$")
_PRIVATE_PATH_RE = re.compile(r"(?<![A-Za-z0-9])(?:/(?:[^\s`]+)|[A-Za-z]:[\\/][^\s`]*)")
_IPV4_RE = re.compile(
    r"(?<![0-9])(?:25[0-5]|2[0-4][0-9]|1?[0-9]{1,2})"
    r"(?:\.(?:25[0-5]|2[0-4][0-9]|1?[0-9]{1,2})){3}(?![0-9])"
)
_IPV6_RE = re.compile(r"(?<![A-Za-z0-9])(?:[0-9A-Fa-f]{1,4}:){2,}[0-9A-Fa-f:]*")
_GITHUB_REMOTE_RE = re.compile(
    r"^(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([^/]+/[^/]+?)(?:\.git)?/?$"
)


class AdmissionError(ValueError):
    """A bounded reason why a release candidate was not admitted."""


def _git(repo: Path, *args: str) -> str:
    try:
        return subprocess.run(
            ("git", "-C", str(repo), *args),
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ).stdout.strip()
    except subprocess.CalledProcessError as error:
        raise AdmissionError("GIT") from error


def _git_file(repo: Path, revision_path: str) -> str:
    try:
        output = subprocess.run(
            ("git", "-C", str(repo), "show", revision_path),
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout
        return output.decode("utf-8")
    except (subprocess.CalledProcessError, UnicodeDecodeError) as error:
        raise AdmissionError("GIT") from error


def require_annotated_tag(repo: Path, tag: str) -> tuple[str, str]:
    """Return the tag object and peeled commit for a protected annotated tag."""
    if not TAG_RE.fullmatch(tag):
        raise AdmissionError("TAG_FORMAT")
    try:
        tag_object = _git(repo, "rev-parse", f"refs/tags/{tag}")
    except AdmissionError as error:
        raise AdmissionError("TAG_MISSING") from error
    if _git(repo, "cat-file", "-t", tag_object) != "tag":
        raise AdmissionError("TAG_NOT_ANNOTATED")
    try:
        commit = _git(repo, "rev-parse", f"refs/tags/{tag}^{{commit}}")
    except AdmissionError as error:
        raise AdmissionError("TAG_NOT_COMMIT") from error
    try:
        _git(repo, "merge-base", "--is-ancestor", commit, "origin/master")
    except AdmissionError as error:
        raise AdmissionError("TAG_NOT_PROTECTED") from error
    return tag_object, commit


def _required_str(evidence: Mapping[str, object], name: str) -> str:
    value = evidence.get(name)
    if not isinstance(value, str) or not value:
        raise AdmissionError(f"EVIDENCE_{name.upper()}")
    return value


def _required_int(evidence: Mapping[str, object], name: str) -> int:
    value = evidence.get(name)
    if type(value) is not int or value < 0:
        raise AdmissionError(f"EVIDENCE_{name.upper()}")
    return value


def _remote_repository_identity(remote: str) -> str | None:
    if remote == CANONICAL_REPOSITORY:
        return remote
    match = _GITHUB_REMOTE_RE.fullmatch(remote)
    return match.group(1) if match else None


def _validate_repository(repo: Path, evidence: Mapping[str, object]) -> str:
    repository = _required_str(evidence, "repository")
    if repository != CANONICAL_REPOSITORY:
        raise AdmissionError("REPOSITORY_IDENTITY")
    try:
        origin = _git(repo, "remote", "get-url", "origin")
    except AdmissionError as error:
        raise AdmissionError("REPOSITORY_ORIGIN") from error
    if _remote_repository_identity(origin) != CANONICAL_REPOSITORY:
        raise AdmissionError("REPOSITORY_ORIGIN")
    return repository


def _validate_tag_event(evidence: Mapping[str, object], tag: str, tag_object: str) -> None:
    event = evidence.get("event")
    if not isinstance(event, Mapping):
        raise AdmissionError("TAG_EVENT")
    if (
        event.get("event_name") != "create"
        or event.get("ref") != tag
        or event.get("ref_type") != "tag"
        or event.get("created") is not True
        or event.get("tag_object") != tag_object
    ):
        raise AdmissionError("TAG_EVENT")


def _validate_release_conflict(evidence: Mapping[str, object]) -> None:
    release = evidence.get("release")
    if not isinstance(release, Mapping) or release.get("exists") is not False:
        raise AdmissionError("RELEASE_CONFLICT")


def _validate_gates(evidence: Mapping[str, object], commit: str) -> tuple[GateResult, ...]:
    raw_gates = evidence.get("gates")
    if not isinstance(raw_gates, Sequence) or isinstance(raw_gates, (str, bytes)):
        raise AdmissionError("GATES")
    by_name: dict[str, GateResult] = {}
    for raw_gate in raw_gates:
        if not isinstance(raw_gate, Mapping):
            raise AdmissionError("GATE")
        name = raw_gate.get("name")
        workflow = raw_gate.get("workflow")
        run_id = raw_gate.get("run_id")
        job_id = raw_gate.get("job_id")
        conclusion = raw_gate.get("conclusion")
        if not isinstance(name, str) or name not in REQUIRED_GATES:
            raise AdmissionError("GATE_NAME")
        if name in by_name:
            raise AdmissionError("GATE_DUPLICATE")
        if workflow != REQUIRED_WORKFLOW:
            raise AdmissionError("GATE_WORKFLOW")
        if type(run_id) is not int or run_id <= 0 or type(job_id) is not int or job_id <= 0:
            raise AdmissionError("GATE_ID")
        if conclusion != "success":
            raise AdmissionError("GATE_CONCLUSION")
        gate_commit = raw_gate.get("commit")
        if gate_commit != commit:
            raise AdmissionError("GATE_COMMIT")
        by_name[name] = GateResult(name, workflow, run_id, job_id, conclusion)
    if set(by_name) != set(REQUIRED_GATES):
        raise AdmissionError("GATE_SET")
    return tuple(by_name[name] for name in REQUIRED_GATES)


def validate_notes_text(notes: str, tag: str, context_values: tuple[str, ...]) -> None:
    """Reject unresolved or generated metadata embedded in reviewed prose."""
    if not notes or "\r" in notes:
        raise AdmissionError("NOTES_FORMAT")
    if _PLACEHOLDER_RE.search(notes):
        raise AdmissionError("NOTES_PLACEHOLDER")
    if _GENERATED_FIELD_RE.search(notes) or _GENERATED_HEADING_RE.search(notes):
        raise AdmissionError("NOTES_GENERATED_FIELD")
    if _PRIVATE_PATH_RE.search(notes):
        raise AdmissionError("NOTES_PRIVACY_PATH")
    if _IPV4_RE.search(notes) or _IPV6_RE.search(notes):
        raise AdmissionError("NOTES_PRIVACY_ADDRESS")
    if any(version != tag for version in _VERSION_RE.findall(notes)):
        raise AdmissionError("NOTES_VERSION")
    if any(value and value in notes for value in context_values):
        raise AdmissionError("NOTES_GENERATED_FIELD")


def _validated_notes(repo: Path, tag: str, commit: str, context_values: tuple[str, ...]) -> str:
    relative = Path("packaging/gba-wifi-link/releases") / tag / "RELEASE-NOTES.md"
    notes_path = repo / relative
    if not notes_path.is_file():
        raise AdmissionError("NOTES_MISSING")
    try:
        _git(repo, "ls-files", "--error-unmatch", "--", str(relative))
        notes = _git_file(repo, f"{commit}:{relative.as_posix()}")
    except AdmissionError as error:
        raise AdmissionError("NOTES_MISSING") from error
    validate_notes_text(notes, tag, context_values)
    return notes


def admit_release(repo: Path, tag: str, evidence: Mapping[str, object]) -> ReleaseContext:
    """Validate local source and exact protected evidence without publishing."""
    if not isinstance(evidence, Mapping):
        raise AdmissionError("EVIDENCE")
    tag_object, commit = require_annotated_tag(repo, tag)
    repository = _validate_repository(repo, evidence)
    _validate_tag_event(evidence, tag, tag_object)
    _validate_release_conflict(evidence)
    evidence_commit = _required_str(evidence, "commit")
    if not SHA1_RE.fullmatch(evidence_commit) or evidence_commit != commit:
        raise AdmissionError("EVIDENCE_COMMIT")
    evidence_epoch = _required_int(evidence, "source_date_epoch")
    try:
        source_date_epoch = int(_git(repo, "show", "-s", "--format=%ct", commit))
    except (AdmissionError, ValueError) as error:
        raise AdmissionError("SOURCE_DATE_EPOCH") from error
    if evidence_epoch != source_date_epoch:
        raise AdmissionError("SOURCE_DATE_EPOCH")
    gates = _validate_gates(evidence, commit)
    notes = _validated_notes(repo, tag, commit, (repository, tag_object, commit))
    match = TAG_RE.fullmatch(tag)
    assert match is not None
    version = ".".join(match.groups())
    return ReleaseContext(
        repository=repository,
        tag=tag,
        tag_object=tag_object,
        commit=commit,
        version=version,
        source_date_epoch=source_date_epoch,
        # The automated release path is currently prerelease-only; in particular,
        # every v0.x tag is necessarily classified as a prerelease.
        prerelease=True,
        gates=gates,
        notes_sha256=hashlib.sha256(notes.encode("utf-8")).hexdigest(),
    )


def verify_remote_tag(context: ReleaseContext, repo: str) -> None:
    """Require a remote tag object and its peeled commit to remain unchanged."""
    if (
        context.repository != CANONICAL_REPOSITORY
        or _remote_repository_identity(repo) != context.repository
    ):
        raise AdmissionError("REMOTE_REPOSITORY")
    remote = (
        f"https://github.com/{repo}.git"
        if repo == CANONICAL_REPOSITORY
        else repo
    )
    try:
        output = subprocess.run(
            (
                "git", "ls-remote", remote,
                f"refs/tags/{context.tag}",
                f"refs/tags/{context.tag}^{{}}",
            ),
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ).stdout
    except subprocess.CalledProcessError as error:
        raise AdmissionError("REMOTE_TAG") from error
    refs = {
        reference: object_id
        for object_id, reference in (
            line.split("\t", 1) for line in output.splitlines() if "\t" in line
        )
    }
    tag_ref = f"refs/tags/{context.tag}"
    peeled_ref = f"{tag_ref}^{{}}"
    if refs.get(tag_ref) != context.tag_object:
        raise AdmissionError("REMOTE_TAG_OBJECT")
    if refs.get(peeled_ref) != context.commit:
        raise AdmissionError("REMOTE_TAG_COMMIT")
