"""Small, fail-closed GitHub CLI adapter for the release publisher."""

from dataclasses import dataclass
import json
import os
from pathlib import Path
import subprocess
import tempfile
from typing import Mapping, Protocol
from urllib.parse import quote

from .model import ReleaseContext


MAX_JSON_BYTES = 1 << 20
MAX_JSON_DEPTH = 20
MAX_ASSETS = 32


class GitHubError(ValueError):
    """A bounded GitHub client failure category."""


class _CommandError(GitHubError):
    def __init__(self, stderr: bytes):
        super().__init__("GITHUB_COMMAND")
        self.stderr = stderr


@dataclass(frozen=True)
class RemoteAsset:
    name: str
    size: int
    sha256: str
    id: int


@dataclass(frozen=True)
class RemoteRelease:
    id: int
    tag: str
    target: str
    body: bytes
    draft: bool
    prerelease: bool
    assets: tuple[RemoteAsset, ...]


class GitHubClient(Protocol):
    def get_release(self, tag: str) -> RemoteRelease | None: ...
    def create_draft(self, context: ReleaseContext, body: bytes) -> RemoteRelease: ...
    def upload(self, release_id: int, path: Path) -> RemoteAsset: ...
    def download_assets(self, release_id: int, output: Path) -> None: ...
    def attest(self, paths: tuple[Path, ...]) -> None: ...
    def publish(self, release_id: int) -> None: ...
    def delete_draft(self, release_id: int) -> None: ...


def _duplicate_free_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise GitHubError("GITHUB_JSON_DUPLICATE")
        result[key] = value
    return result


def _check_json_bounds(value: object, depth: int = 0) -> None:
    if depth > MAX_JSON_DEPTH:
        raise GitHubError("GITHUB_JSON_DEPTH")
    if isinstance(value, dict):
        if len(value) > MAX_ASSETS:
            raise GitHubError("GITHUB_JSON_OBJECT")
        for key, child in value.items():
            if not isinstance(key, str) or len(key) > 256:
                raise GitHubError("GITHUB_JSON_OBJECT")
            _check_json_bounds(child, depth + 1)
    elif isinstance(value, list):
        if len(value) > MAX_ASSETS:
            raise GitHubError("GITHUB_JSON_ARRAY")
        for child in value:
            _check_json_bounds(child, depth + 1)
    elif isinstance(value, str) and len(value.encode("utf-8")) > MAX_JSON_BYTES:
        raise GitHubError("GITHUB_JSON_STRING")


def _json(data: bytes) -> object:
    if len(data) > MAX_JSON_BYTES:
        raise GitHubError("GITHUB_JSON_SIZE")
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=_duplicate_free_object,
                           parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
    except (UnicodeDecodeError, ValueError, json.JSONDecodeError) as error:
        if isinstance(error, GitHubError):
            raise
        raise GitHubError("GITHUB_JSON") from error
    _check_json_bounds(value)
    return value


def _required_str(value: Mapping[str, object], name: str, maximum: int = MAX_JSON_BYTES) -> str:
    result = value.get(name)
    if not isinstance(result, str) or not result or len(result.encode("utf-8")) > maximum:
        raise GitHubError("GITHUB_RELEASE")
    return result


def _required_int(value: Mapping[str, object], name: str) -> int:
    result = value.get(name)
    if type(result) is not int or result <= 0:
        raise GitHubError("GITHUB_RELEASE")
    return result


def _remote_asset(value: object) -> RemoteAsset:
    if not isinstance(value, Mapping):
        raise GitHubError("GITHUB_ASSET")
    name = _required_str(value, "name", 256)
    if Path(name).name != name or name in {".", ".."}:
        raise GitHubError("GITHUB_ASSET")
    size = value.get("size")
    asset_id = value.get("id")
    digest = value.get("digest")
    if (
        type(size) is not int or size < 0 or type(asset_id) is not int or asset_id <= 0
        or not isinstance(digest, str) or not digest.startswith("sha256:")
        or len(digest) != 71
        or any(character not in "0123456789abcdef" for character in digest[7:])
    ):
        raise GitHubError("GITHUB_ASSET")
    return RemoteAsset(name, size, digest[7:], asset_id)


def _remote_release(value: object) -> RemoteRelease:
    if not isinstance(value, Mapping):
        raise GitHubError("GITHUB_RELEASE")
    raw_assets = value.get("assets")
    if not isinstance(raw_assets, list) or len(raw_assets) > MAX_ASSETS:
        raise GitHubError("GITHUB_RELEASE")
    body = value.get("body")
    draft = value.get("draft")
    prerelease = value.get("prerelease")
    if not isinstance(body, str) or type(draft) is not bool or type(prerelease) is not bool:
        raise GitHubError("GITHUB_RELEASE")
    try:
        body_bytes = body.encode("utf-8")
    except UnicodeEncodeError as error:
        raise GitHubError("GITHUB_RELEASE") from error
    if len(body_bytes) > MAX_JSON_BYTES:
        raise GitHubError("GITHUB_RELEASE")
    assets = tuple(_remote_asset(item) for item in raw_assets)
    if len({asset.id for asset in assets}) != len(assets) or len({asset.name for asset in assets}) != len(assets):
        raise GitHubError("GITHUB_ASSET")
    return RemoteRelease(
        _required_int(value, "id"), _required_str(value, "tag_name", 256),
        _required_str(value, "target_commitish", 256), body_bytes, draft,
        prerelease, assets,
    )


class GhClient:
    """GitHub CLI implementation with no shell parsing or implicit repository."""

    def __init__(self, repository: str, *, gh: str = "gh",
                 env: Mapping[str, str] | None = None):
        if not isinstance(repository, str) or not repository:
            raise GitHubError("GITHUB_REPOSITORY")
        self.repository = repository
        self.gh = gh
        self.env = os.environ.copy()
        if env is not None:
            self.env.update(env)

    def _run(self, *args: str) -> bytes:
        try:
            return subprocess.run(
                [self.gh, *args, "--repo", self.repository], check=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=self.env,
            ).stdout
        except (OSError, subprocess.CalledProcessError) as error:
            if isinstance(error, subprocess.CalledProcessError):
                raise _CommandError(error.stderr or b"") from error
            raise GitHubError("GITHUB_COMMAND") from error

    def get_release(self, tag: str) -> RemoteRelease | None:
        if not isinstance(tag, str) or not tag:
            raise GitHubError("GITHUB_TAG")
        try:
            data = self._run("api", f"repos/{self.repository}/releases/tags/{quote(tag, safe='')}")
        except _CommandError as error:
            if b"404" in error.stderr:
                return None
            raise GitHubError("GITHUB_COMMAND") from error
        return _remote_release(_json(data))

    def create_draft(self, context: ReleaseContext, body: bytes) -> RemoteRelease:
        if not isinstance(body, bytes) or len(body) > MAX_JSON_BYTES:
            raise GitHubError("GITHUB_BODY")
        try:
            text = body.decode("utf-8")
        except UnicodeDecodeError as error:
            raise GitHubError("GITHUB_BODY") from error
        payload = json.dumps({
            "body": text,
            "draft": True,
            "prerelease": context.prerelease,
            "tag_name": context.tag,
            "target_commitish": context.commit,
        }, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
        with tempfile.NamedTemporaryFile(prefix="gba-wifi-link-release-", suffix=".json") as source:
            source.write(payload)
            source.flush()
            response = self._run("api", "--method", "POST",
                                 f"repos/{self.repository}/releases", "--input", source.name)
        return _remote_release(_json(response))

    def upload(self, release_id: int, path: Path) -> RemoteAsset:
        if type(release_id) is not int or release_id <= 0 or not path.is_file():
            raise GitHubError("GITHUB_UPLOAD")
        response = self._run(
            "api", "--hostname", "uploads.github.com", "--method", "POST",
            f"repos/{self.repository}/releases/{release_id}/assets?name={quote(path.name, safe='')}",
            "--input", str(path),
        )
        return _remote_asset(_json(response))

    def download_assets(self, release_id: int, output: Path) -> None:
        if type(release_id) is not int or release_id <= 0 or not output.is_dir():
            raise GitHubError("GITHUB_DOWNLOAD")
        listing = _json(self._run("api", f"repos/{self.repository}/releases/{release_id}/assets"))
        if not isinstance(listing, list) or len(listing) > MAX_ASSETS:
            raise GitHubError("GITHUB_ASSET")
        assets = tuple(_remote_asset(item) for item in listing)
        if len({asset.name for asset in assets}) != len(assets):
            raise GitHubError("GITHUB_ASSET")
        for asset in assets:
            destination = output / asset.name
            if destination.exists() or destination.is_symlink():
                raise GitHubError("GITHUB_DOWNLOAD")
            self._run(
                "api", f"repos/{self.repository}/releases/assets/{asset.id}",
                "--header", "Accept: application/octet-stream", "--output", str(destination),
            )

    def attest(self, paths: tuple[Path, ...]) -> None:
        if len(paths) != 2 or any(not path.is_file() for path in paths):
            raise GitHubError("GITHUB_ATTEST")
        self._run("attestation", "create", *(str(path) for path in paths))

    def publish(self, release_id: int) -> None:
        if type(release_id) is not int or release_id <= 0:
            raise GitHubError("GITHUB_RELEASE")
        self._run("api", "--method", "PATCH",
                  f"repos/{self.repository}/releases/{release_id}", "--field", "draft=false")

    def delete_draft(self, release_id: int) -> None:
        if type(release_id) is not int or release_id <= 0:
            raise GitHubError("GITHUB_RELEASE")
        self._run("api", "--method", "DELETE", f"repos/{self.repository}/releases/{release_id}")
