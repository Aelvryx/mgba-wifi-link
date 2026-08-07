"""Fail-closed, transactional publication of an already verified release set."""

from dataclasses import dataclass
from pathlib import Path
import tempfile

from .github import GitHubClient, RemoteAsset, RemoteRelease
from .model import ReleaseAsset, ReleaseSet
from .verifier import VerificationError, verify_release


class PublishError(ValueError):
    """A bounded failure before public publication."""


class ReleaseConflict(PublishError):
    """An immutable remote state differs from the exact canonical release."""


@dataclass(frozen=True)
class PublishResult:
    release_id: int
    published: bool
    reused: bool


def _asset_matches(remote: RemoteAsset, local: ReleaseAsset) -> bool:
    return remote.name == local.name and remote.size == local.size and remote.sha256 == local.sha256


def _metadata_matches(remote: RemoteRelease, release_set: ReleaseSet, body: bytes,
                      *, draft: bool) -> bool:
    context = release_set.context
    return (
        remote.tag == context.tag
        and remote.target == context.commit
        and remote.body == body
        and remote.draft is draft
        and remote.prerelease is context.prerelease
    )


def _assets_match(remote: RemoteRelease, release_set: ReleaseSet) -> bool:
    local = release_set.assets
    return len(remote.assets) == len(local) and all(
        _asset_matches(actual, expected) for actual, expected in zip(remote.assets, local)
    )


def _failure(conflict: bool, category: str) -> None:
    if conflict:
        raise ReleaseConflict("RELEASE_CONFLICT")
    raise PublishError(category)


def _verify_remote(client: GitHubClient, release_set: ReleaseSet, body: bytes,
                   remote: RemoteRelease, *, draft: bool, conflict: bool) -> None:
    if not _metadata_matches(remote, release_set, body, draft=draft):
        _failure(conflict, "PUBLISH_METADATA")
    if not _assets_match(remote, release_set):
        _failure(conflict, "PUBLISH_ASSETS")
    with tempfile.TemporaryDirectory(prefix="gba-wifi-link-release-readback-") as directory:
        try:
            client.download_assets(remote.id, Path(directory))
            readback = verify_release(Path(directory), release_set.context)
        except (OSError, VerificationError, ValueError) as error:
            if conflict:
                raise ReleaseConflict("RELEASE_CONFLICT") from error
            raise PublishError("PUBLISH_DOWNLOAD") from error
    if readback.assets != release_set.assets:
        _failure(conflict, "PUBLISH_DOWNLOAD")


def _safe_cleanup(client: GitHubClient, created: RemoteRelease, release_set: ReleaseSet) -> None:
    """Delete only the exact same private draft this invocation created."""
    try:
        current = client.get_release(release_set.context.tag)
        if (
            current is not None
            and current.id == created.id
            and current.draft
            and current.tag == release_set.context.tag
            and current.target == release_set.context.commit
        ):
            client.delete_draft(created.id)
    except Exception:
        # Cleanup evidence must never obscure the fail-closed primary outcome.
        pass


def _verified_local(release_set: ReleaseSet) -> ReleaseSet:
    if release_set.directory is None:
        raise PublishError("PUBLISH_INPUT")
    verified = verify_release(release_set.directory, release_set.context)
    if verified.assets != release_set.assets:
        raise PublishError("PUBLISH_INPUT")
    return verified


def _attestation_paths(release_set: ReleaseSet) -> tuple[Path, Path]:
    assert release_set.directory is not None
    core = release_set.directory / "mgba_libretro_android.so"
    archives = [asset.name for asset in release_set.assets if asset.name.endswith(".zip")]
    if len(archives) != 1:
        raise PublishError("PUBLISH_INPUT")
    return core, release_set.directory / archives[0]


def publish_release(client: GitHubClient, release_set: ReleaseSet, body: bytes) -> PublishResult:
    """Publish one exact release, retaining no mutation path for conflicts or reruns."""
    if not isinstance(body, bytes):
        raise PublishError("PUBLISH_BODY")
    release_set = _verified_local(release_set)
    existing = client.get_release(release_set.context.tag)
    if existing is not None:
        if existing.draft:
            raise ReleaseConflict("RELEASE_CONFLICT")
        _verify_remote(client, release_set, body, existing, draft=False, conflict=True)
        return PublishResult(existing.id, True, True)

    created: RemoteRelease | None = None
    try:
        created = client.create_draft(release_set.context, body)
        if not _metadata_matches(created, release_set, body, draft=True) or created.assets:
            raise PublishError("PUBLISH_METADATA")
        for asset in release_set.assets:
            assert release_set.directory is not None
            uploaded = client.upload(created.id, release_set.directory / asset.name)
            if not _asset_matches(uploaded, asset):
                raise PublishError("PUBLISH_UPLOAD")
        remote = client.get_release(release_set.context.tag)
        if remote is None or remote.id != created.id:
            raise PublishError("PUBLISH_READBACK")
        _verify_remote(client, release_set, body, remote, draft=True, conflict=False)
        client.attest(_attestation_paths(release_set))
    except Exception:
        if created is not None:
            _safe_cleanup(client, created, release_set)
        raise

    try:
        client.publish(created.id)
    except Exception as error:
        remote = client.get_release(release_set.context.tag)
        if remote is not None and not remote.draft:
            _verify_remote(client, release_set, body, remote, draft=False, conflict=True)
            return PublishResult(remote.id, True, False)
        raise ReleaseConflict("RELEASE_CONFLICT") from error

    remote = client.get_release(release_set.context.tag)
    if remote is None or remote.id != created.id:
        raise ReleaseConflict("RELEASE_CONFLICT")
    _verify_remote(client, release_set, body, remote, draft=False, conflict=True)
    return PublishResult(remote.id, True, False)
