"""Fail-closed admission checks for annotated GBA Wi-Fi Link release tags."""

from collections.abc import Mapping, Sequence
import hashlib
from pathlib import Path
import re
import subprocess

from .model import BuildEvidence, GateResult, ReleaseContext


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
_GENERATED_FIELD_NAMES = frozenset({
    "repository", "tag", "annotated tag object", "peeled commit",
    "source", "source identity", "build", "build identity", "source date epoch",
    "version", "workflow", "workflow run", "artifact", "artifacts", "asset",
    "assets", "checksum", "checksums", "release provenance", "provenance",
    "compatibility", "qualification",
})
_GENERATED_HEADING_NAMES = frozenset({
    "compatibility", "qualification", "source", "source and verification",
    "source and provenance", "build", "build provenance", "workflow",
    "workflow evidence", "checksum", "checksums", "provenance",
    "release provenance", "artifact", "artifacts", "asset", "assets",
    "release asset", "release assets",
})
_MARKDOWN_HEADING_RE = re.compile(r"^\s*#+\s+(.+?)\s*#*\s*$")
_FIELD_LABEL_RE = re.compile(r"^\s*([A-Za-z][A-Za-z -]*?)\s*:")
_PUBLIC_URL_RE = re.compile(r"https?://[^\s`]+")
_PRIVATE_PATH_RE = re.compile(r"(?<![A-Za-z0-9])(?:~[\\/]|/(?:[^\s`]+)|[A-Za-z]:[\\/][^\s`]*)")
_TRAVERSAL_PATH_RE = re.compile(r"(?<![A-Za-z0-9])\.\.[\\/]")
_IPV4_RE = re.compile(
    r"(?<![0-9])(?:25[0-5]|2[0-4][0-9]|1?[0-9]{1,2})"
    r"(?:\.(?:25[0-5]|2[0-4][0-9]|1?[0-9]{1,2})){3}(?![0-9])"
)
_IPV6_RE = re.compile(r"(?<![A-Za-z0-9])(?:[0-9A-Fa-f]{1,4}:){2,}[0-9A-Fa-f:]*")
_ROM_BIOS_RE = re.compile(r"(?i)(?:^\s*(?:rom|bios)\s*:|\b(?:rom|bios)\b[^\n]*(?:sha(?:-?256)?|hash|crc|dump|identity))")
_SAVE_RE = re.compile(r"(?i)\b(?:save[ -]?(?:file|state)|\.sav)\b[^\n]*(?:attached|identity|sha(?:-?256)?|hash|dump|private|data)")
_INPUT_RE = re.compile(r"(?i)\b(?:raw input|input recording|input history)\b")
_LOG_RE = re.compile(r"(?i)\b(?:endpoint|frontend|retroarch) log\b")
_DEVICE_RE = re.compile(r"(?i)\b(?:device|phone) (?:serial|nickname|id|name)\b")
_COMMERCIAL_RE = re.compile(r"(?i)\bcommercial (?:game|title|evidence)\b")
_SECRET_RE = re.compile(r"(?i)\b(?:access )?(?:api[_ -]?key|token|secret|password)(?:\s*[:=]|\s+\S+)")
_GITHUB_REMOTE_RE = re.compile(
    r"^(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([^/]+/[^/]+?)(?:\.git)?/?$"
)
_ACTION_PIN_RE = re.compile(r"^[A-Za-z0-9._/-]+@[0-9a-f]{40}$")
_TOOLCHAIN_PIN_RE = re.compile(r"^[A-Za-z0-9._/-]+@[A-Za-z0-9._-]+\+sha256:[0-9a-f]{64}$")


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


def _validate_build(evidence: Mapping[str, object]) -> BuildEvidence:
    raw_build = evidence.get("build")
    if not isinstance(raw_build, Mapping) or set(raw_build) != {
        "configuration", "pinned_actions", "pinned_toolchains", "runner_image",
    }:
        raise AdmissionError("BUILD_EVIDENCE")
    runner_image = raw_build["runner_image"]
    actions = raw_build["pinned_actions"]
    toolchains = raw_build["pinned_toolchains"]
    configuration = raw_build["configuration"]
    if (
        not isinstance(runner_image, str)
        or not runner_image
        or not isinstance(actions, Sequence)
        or isinstance(actions, (str, bytes))
        or not actions
        or not all(isinstance(action, str) and _ACTION_PIN_RE.fullmatch(action) for action in actions)
        or tuple(actions) != tuple(sorted(actions))
        or len(set(actions)) != len(actions)
        or not isinstance(toolchains, Sequence)
        or isinstance(toolchains, (str, bytes))
        or not toolchains
        or not all(isinstance(toolchain, str) and _TOOLCHAIN_PIN_RE.fullmatch(toolchain) for toolchain in toolchains)
        or tuple(toolchains) != tuple(sorted(toolchains))
        or len(set(toolchains)) != len(toolchains)
        or not isinstance(configuration, Mapping)
        or not configuration
        or any(not isinstance(key, str) or not key or not isinstance(value, str) or not value
               for key, value in configuration.items())
    ):
        raise AdmissionError("BUILD_EVIDENCE")
    return BuildEvidence(runner_image, tuple(actions), tuple(toolchains), tuple(sorted(configuration.items())))


def _notes_line_category(line: str) -> str | None:
    """Classify a single reviewed-prose line without exposing its contents."""
    heading = _MARKDOWN_HEADING_RE.fullmatch(line)
    if heading and heading.group(1).casefold() in _GENERATED_HEADING_NAMES:
        return "NOTES_GENERATED_FIELD"
    label = _FIELD_LABEL_RE.match(line)
    if label and label.group(1).casefold() in _GENERATED_FIELD_NAMES:
        return "NOTES_GENERATED_FIELD"
    public_url_free = _PUBLIC_URL_RE.sub("", line)
    if _PRIVATE_PATH_RE.search(public_url_free) or _TRAVERSAL_PATH_RE.search(public_url_free):
        return "NOTES_PRIVACY_PATH"
    if _IPV4_RE.search(line) or _IPV6_RE.search(line):
        return "NOTES_PRIVACY_ADDRESS"
    for pattern, reason in (
        (_ROM_BIOS_RE, "NOTES_PRIVACY_ROM_BIOS"),
        (_SAVE_RE, "NOTES_PRIVACY_SAVE"),
        (_INPUT_RE, "NOTES_PRIVACY_INPUT"),
        (_LOG_RE, "NOTES_PRIVACY_LOG"),
        (_DEVICE_RE, "NOTES_PRIVACY_DEVICE"),
        (_COMMERCIAL_RE, "NOTES_PRIVACY_COMMERCIAL"),
        (_SECRET_RE, "NOTES_PRIVACY_SECRET"),
    ):
        if pattern.search(line):
            return reason
    return None


def validate_notes_text(notes: str, tag: str, context_values: tuple[str, ...]) -> None:
    """Reject unresolved or generated metadata embedded in reviewed prose."""
    if not notes or "\r" in notes:
        raise AdmissionError("NOTES_FORMAT")
    if _PLACEHOLDER_RE.search(notes):
        raise AdmissionError("NOTES_PLACEHOLDER")
    for line in notes.splitlines():
        category = _notes_line_category(line)
        if category:
            raise AdmissionError(category)
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
    build = _validate_build(evidence)
    notes = _validated_notes(repo, tag, commit, (tag_object, commit))
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
        build=build,
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
