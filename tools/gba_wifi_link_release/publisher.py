"""Fail-closed, transactional publication of an already verified release set."""

from dataclasses import dataclass
from contextlib import contextmanager
import hashlib
import os
from pathlib import Path
import stat
import tempfile
from typing import Iterator

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


def _snapshot_asset(source: Path, destination: Path, asset: ReleaseAsset) -> None:
    """Capture one regular source through a no-follow descriptor into a private file."""
    if not hasattr(os, "O_NOFOLLOW"):
        raise PublishError("PUBLISH_SUBJECT")
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | os.O_NOFOLLOW
    try:
        descriptor = os.open(source, flags)
    except OSError as error:
        raise PublishError("PUBLISH_SUBJECT") from error
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise PublishError("PUBLISH_SUBJECT")
        output = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            digest = hashlib.sha256()
            size = 0
            while data := os.read(descriptor, 1 << 16):
                size += len(data)
                digest.update(data)
                remaining = memoryview(data)
                while remaining:
                    written = os.write(output, remaining)
                    if written <= 0:
                        raise OSError("snapshot write")
                    remaining = remaining[written:]
        finally:
            os.close(output)
    except PublishError:
        raise
    except OSError as error:
        raise PublishError("PUBLISH_SUBJECT") from error
    finally:
        os.close(descriptor)
    if size != asset.size or digest.hexdigest() != asset.sha256:
        raise PublishError("PUBLISH_SUBJECT")


@contextmanager
def _snapshot_release(release_set: ReleaseSet) -> Iterator[ReleaseSet]:
    assert release_set.directory is not None
    with tempfile.TemporaryDirectory(prefix="gba-wifi-link-release-subject-") as directory:
        root = Path(directory)
        for asset in release_set.assets:
            _snapshot_asset(release_set.directory / asset.name, root / asset.name, asset)
        yield ReleaseSet(release_set.context, release_set.assets, root)


def _publish_verified(client: GitHubClient, release_set: ReleaseSet,
                      body: bytes) -> PublishResult:
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


def publish_release(client: GitHubClient, release_set: ReleaseSet, body: bytes) -> PublishResult:
    """Publish one exact release, retaining no mutation path for conflicts or reruns."""
    if not isinstance(body, bytes):
        raise PublishError("PUBLISH_BODY")
    release_set = _verified_local(release_set)
    with _snapshot_release(release_set) as snapshot:
        return _publish_verified(client, snapshot, body)
