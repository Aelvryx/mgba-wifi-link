"""Early, read-only verification of an immutable public release."""

from dataclasses import dataclass
from enum import Enum
import hashlib
from io import BytesIO
import json
from pathlib import Path
import tempfile
from typing import NoReturn
import zipfile

from .admission import REQUIRED_GATES, REQUIRED_WORKFLOW
from .github import (
    AttestationSubject,
    CANONICAL_SIGNER_WORKFLOW,
    GitHubClient,
    RemoteRelease,
)
from .model import (
    ActualBuildEvidence,
    BuildEvidence,
    GateResult,
    ReleaseAsset,
    ReleaseContext,
)
from .provenance import canonical_json
from .render import render_release_body
from .verifier import verify_release


class ExistingReleaseError(ValueError):
    """A public release exists but is not one coherent retained record."""


class ExistingReleaseStatus(Enum):
    PROCEED = "proceed"
    REUSED = "reused"


@dataclass(frozen=True)
class ExistingReleaseResult:
    status: ExistingReleaseStatus
    release_id: int | None = None
    retained_context: ReleaseContext | None = None


def _fail(error: Exception | None = None) -> NoReturn:
    if error is None:
        raise ExistingReleaseError("EXISTING_RELEASE_CONFLICT")
    raise ExistingReleaseError("EXISTING_RELEASE_CONFLICT") from error


def _canonical_document(data: bytes, required: set[str]) -> dict[str, object]:
    try:
        value = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        _fail(error)
    if not isinstance(value, dict) or set(value) != required or canonical_json(value) != data:
        _fail()
    return value


def _asset(value: object) -> ReleaseAsset:
    if not isinstance(value, dict) or set(value) != {"name", "sha256", "size"}:
        _fail()
    try:
        return ReleaseAsset(value["name"], value["size"], value["sha256"])
    except (KeyError, TypeError) as error:
        _fail(error)


def _actual_build(value: object) -> ActualBuildEvidence:
    required = {
        "cmake_version", "compiler_sha256", "compiler_version", "configuration",
        "core", "job_id", "ndk_revision", "ndk_source_properties_sha256",
        "ninja_version", "pinned_actions", "role", "run_id", "runner_image_os",
        "runner_image_version", "source_commit", "source_date_epoch",
    }
    if not isinstance(value, dict) or set(value) != required:
        _fail()
    configuration = value["configuration"]
    if not isinstance(configuration, dict):
        _fail()
    try:
        return ActualBuildEvidence(
            role=value["role"], run_id=value["run_id"], job_id=value["job_id"],
            runner_image_os=value["runner_image_os"],
            runner_image_version=value["runner_image_version"],
            ndk_revision=value["ndk_revision"],
            ndk_source_properties_sha256=value["ndk_source_properties_sha256"],
            compiler_sha256=value["compiler_sha256"],
            compiler_version=value["compiler_version"],
            cmake_version=value["cmake_version"], ninja_version=value["ninja_version"],
            source_commit=value["source_commit"],
            source_date_epoch=value["source_date_epoch"],
            configuration=tuple(configuration.items()),
            core=_asset(value["core"]),
            pinned_actions=tuple(value["pinned_actions"]),
        )
    except (KeyError, TypeError) as error:
        _fail(error)


def _build(value: object) -> BuildEvidence:
    required = {
        "actual_builds", "configuration", "pinned_actions",
        "pinned_toolchains", "runner_image",
    }
    if not isinstance(value, dict) or set(value) != required:
        _fail()
    configuration = value["configuration"]
    if not isinstance(configuration, dict):
        _fail()
    try:
        return BuildEvidence(
            value["runner_image"], tuple(value["pinned_actions"]),
            tuple(value["pinned_toolchains"]), tuple(configuration.items()),
            tuple(_actual_build(item) for item in value["actual_builds"]),
        )
    except (KeyError, TypeError) as error:
        _fail(error)


def _gates(value: object) -> tuple[GateResult, ...]:
    if not isinstance(value, list):
        _fail()
    gates: list[GateResult] = []
    required = {"conclusion", "job_id", "name", "run_id", "workflow"}
    try:
        for item in value:
            if not isinstance(item, dict) or set(item) != required:
                _fail()
            gates.append(GateResult(
                item["name"], item["workflow"], item["run_id"], item["job_id"],
                item["conclusion"],
            ))
    except (KeyError, TypeError) as error:
        _fail(error)
    if (
        tuple(gate.name for gate in gates) != REQUIRED_GATES
        or any(gate.workflow != REQUIRED_WORKFLOW for gate in gates)
    ):
        _fail()
    return tuple(gates)


def _retained_context(directory: Path) -> ReleaseContext:
    release_document = _canonical_document(
        (directory / "RELEASE-PROVENANCE.json").read_bytes(),
        {"build", "payloads", "schema", "source"},
    )
    archive_names = tuple(path for path in directory.iterdir() if path.name.endswith(".zip"))
    if len(archive_names) != 1:
        _fail()
    try:
        with zipfile.ZipFile(BytesIO(archive_names[0].read_bytes())) as archive:
            build_bytes = archive.read("BUILD-PROVENANCE.json")
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        _fail(error)
    build_document = _canonical_document(
        build_bytes, {"build", "gates", "schema", "siblings", "source"},
    )
    if (
        release_document["schema"] != 1
        or build_document["schema"] != 1
        or release_document["source"] != build_document["source"]
        or release_document["build"] != build_document["build"]
    ):
        _fail()
    source = release_document["source"]
    if not isinstance(source, dict) or set(source) != {
        "commit", "notes_sha256", "prerelease", "repository",
        "source_date_epoch", "tag", "tag_object", "version",
    }:
        _fail()
    try:
        return ReleaseContext(
            repository=source["repository"], tag=source["tag"],
            tag_object=source["tag_object"], commit=source["commit"],
            version=source["version"], source_date_epoch=source["source_date_epoch"],
            prerelease=source["prerelease"], gates=_gates(build_document["gates"]),
            notes_sha256=source["notes_sha256"], build=_build(release_document["build"]),
        )
    except (KeyError, TypeError) as error:
        _fail(error)


def _same_immutable_source(expected: ReleaseContext, retained: ReleaseContext) -> bool:
    return (
        expected.repository == retained.repository
        and expected.tag == retained.tag
        and expected.tag_object == retained.tag_object
        and expected.commit == retained.commit
        and expected.version == retained.version
        and expected.source_date_epoch == retained.source_date_epoch
        and expected.prerelease == retained.prerelease
        and expected.notes_sha256 == retained.notes_sha256
    )


def _remote_assets_match(remote: RemoteRelease,
                         local: tuple[ReleaseAsset, ...]) -> bool:
    remote_by_name = {asset.name: asset for asset in remote.assets}
    return (
        len(remote_by_name) == len(remote.assets) == len(local)
        and set(remote_by_name) == {asset.name for asset in local}
        and all(
            remote_by_name[asset.name].size == asset.size
            and remote_by_name[asset.name].sha256 == asset.sha256
            for asset in local
        )
    )


def verify_existing_public_release(
    client: GitHubClient,
    expected_context: ReleaseContext,
    notes: bytes,
) -> ExistingReleaseResult:
    """Return before builds when the retained public first-run record is exact."""
    try:
        remote = client.get_release(expected_context.tag)
        if remote is None:
            return ExistingReleaseResult(ExistingReleaseStatus.PROCEED)
        if (
            remote.draft
            or remote.tag != expected_context.tag
            or remote.target != expected_context.commit
            or remote.prerelease != expected_context.prerelease
        ):
            _fail()
        if not isinstance(notes, bytes):
            _fail()
        with tempfile.TemporaryDirectory(prefix="gba-wifi-link-existing-release-") as temporary:
            directory = Path(temporary)
            client.download_assets(remote.id, directory)
            retained = _retained_context(directory)
            if not _same_immutable_source(expected_context, retained):
                _fail()
            verified = verify_release(directory, retained)
            if not _remote_assets_match(remote, verified.assets):
                _fail()
            try:
                notes_text = notes.decode("utf-8")
            except UnicodeDecodeError as error:
                _fail(error)
            if (
                hashlib.sha256(notes).hexdigest() != retained.notes_sha256
                or remote.body != render_release_body(retained, notes_text)
            ):
                _fail()
            archive = next(asset for asset in verified.assets if asset.name.endswith(".zip"))
            core = next(asset for asset in verified.assets
                        if asset.name == "mgba_libretro_android.so")
            subjects = tuple(
                AttestationSubject(asset.name, directory / asset.name,
                                   asset.size, asset.sha256)
                for asset in (core, archive)
            )
            client.verify_attestations(
                subjects,
                source_digest=retained.commit,
                signer_workflow=CANONICAL_SIGNER_WORKFLOW,
            )
            return ExistingReleaseResult(
                ExistingReleaseStatus.REUSED, remote.id, retained,
            )
    except ExistingReleaseError:
        raise
    except (OSError, TypeError, ValueError) as error:
        _fail(error)
