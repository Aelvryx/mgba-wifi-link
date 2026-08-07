"""Fail-closed public staging-tree privacy validation."""

import hashlib
import json
import os
from pathlib import Path
import re
import stat

from .model import REQUIRED_BUILD_CONFIGURATION, ReleaseContract
from .provenance import canonical_json
from .text_policy import classify_private_text


_MAX_TEXT_BYTES = 1_048_576
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
_TAG_RE = re.compile(r"^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
_ACTION_PIN_RE = re.compile(r"^[A-Za-z0-9._/-]+@[0-9a-f]{40}$")
_TOOLCHAIN_PIN_RE = re.compile(r"^[A-Za-z0-9._/-]+@[A-Za-z0-9._-]+\+sha256:[0-9a-f]{64}$")


class PrivacyError(ValueError):
    """A category-only public material rejection."""


def _text_category(text: str) -> str | None:
    category = classify_private_text(text)
    return f"PRIVACY_{category}" if category else None


def _resolve_public_names(root: Path, contract: ReleaseContract) -> tuple[str, ...]:
    names = list(contract.public_assets)
    archive_template = next(name for name in names if "{tag}" in name)
    archives = [path.name for path in root.iterdir()
                if re.fullmatch(r"mgba-gba-wifi-link-v[0-9]+\.[0-9]+\.[0-9]+-android-arm64\.zip", path.name)]
    if len(archives) != 1:
        raise PrivacyError("PRIVACY_FILE_SET")
    return tuple(name if name != archive_template else archives[0] for name in names)


def _read_text(path: Path) -> str:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise PrivacyError("PRIVACY_FILE_READ") from error
    if len(data) > _MAX_TEXT_BYTES:
        raise PrivacyError("PRIVACY_TEXT_SIZE")
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise PrivacyError("PRIVACY_TEXT_ENCODING") from error


def _validate_release_provenance(path: Path, root: Path, expected_names: tuple[str, ...]) -> None:
    text = _read_text(path)
    category = _text_category(text)
    if category:
        raise PrivacyError(category)
    try:
        value = json.loads(text)
    except json.JSONDecodeError as error:
        raise PrivacyError("PRIVACY_JSON") from error
    if not isinstance(value, dict) or tuple(sorted(value)) != ("build", "payloads", "schema", "source") or value.get("schema") != 1:
        raise PrivacyError("PRIVACY_FIELD")
    if canonical_json(value) != text.encode("utf-8"):
        raise PrivacyError("PRIVACY_JSON")
    build = value.get("build")
    if (
        not isinstance(build, dict)
        or set(build) != {"configuration", "pinned_actions", "pinned_toolchains", "runner_image"}
        or not isinstance(build["runner_image"], str)
        or not build["runner_image"]
        or not isinstance(build["pinned_actions"], list)
        or not build["pinned_actions"]
        or build["pinned_actions"] != sorted(build["pinned_actions"])
        or len(set(build["pinned_actions"])) != len(build["pinned_actions"])
        or not all(isinstance(action, str) and _ACTION_PIN_RE.fullmatch(action)
                   for action in build["pinned_actions"])
        or not isinstance(build["pinned_toolchains"], list)
        or not build["pinned_toolchains"]
        or build["pinned_toolchains"] != sorted(build["pinned_toolchains"])
        or len(set(build["pinned_toolchains"])) != len(build["pinned_toolchains"])
        or not all(isinstance(toolchain, str) and _TOOLCHAIN_PIN_RE.fullmatch(toolchain)
                   for toolchain in build["pinned_toolchains"])
        or not isinstance(build["configuration"], dict)
        or not build["configuration"]
        or tuple(build["configuration"].items()) != REQUIRED_BUILD_CONFIGURATION
    ):
        raise PrivacyError("PRIVACY_FIELD")
    source = value.get("source")
    required_source = {"commit", "notes_sha256", "prerelease", "repository", "source_date_epoch", "tag", "tag_object", "version"}
    if not isinstance(source, dict) or set(source) != required_source:
        raise PrivacyError("PRIVACY_FIELD")
    tag = source["tag"]
    if (
        source["repository"] != "Aelvryx/mgba-wifi-link"
        or not isinstance(tag, str)
        or not _TAG_RE.fullmatch(tag)
        or tag != _archive_tag(expected_names)
        or source["version"] != tag[1:]
        or not isinstance(source["tag_object"], str)
        or not _SHA1_RE.fullmatch(source["tag_object"])
        or not isinstance(source["commit"], str)
        or not _SHA1_RE.fullmatch(source["commit"])
        or type(source["source_date_epoch"]) is not int
        or source["source_date_epoch"] < 0
        or type(source["prerelease"]) is not bool
        or not isinstance(source["notes_sha256"], str)
        or not _SHA256_RE.fullmatch(source["notes_sha256"])
    ):
        raise PrivacyError("PRIVACY_FIELD")
    payloads = value.get("payloads")
    if not isinstance(payloads, list) or len(payloads) != 5 or tuple(item.get("name") if isinstance(item, dict) else None for item in payloads) != expected_names:
        raise PrivacyError("PRIVACY_FIELD")
    for asset in payloads:
        if (
            not isinstance(asset, dict)
            or set(asset) != {"name", "sha256", "size"}
            or type(asset["size"]) is not int
            or asset["size"] < 0
            or not isinstance(asset["sha256"], str)
            or not _SHA256_RE.fullmatch(asset["sha256"])
        ):
            raise PrivacyError("PRIVACY_FIELD")
        candidate = root / asset["name"]
        if not candidate.is_file() or candidate.stat().st_size != asset["size"]:
            raise PrivacyError("PRIVACY_HASH")
        if hashlib.sha256(candidate.read_bytes()).hexdigest() != asset["sha256"]:
            raise PrivacyError("PRIVACY_HASH")


def validate_public_tree(root: Path, contract: ReleaseContract) -> None:
    """Require exactly the declared regular public files and safe public text."""
    try:
        root_stat = root.lstat()
    except OSError as error:
        raise PrivacyError("PRIVACY_ROOT") from error
    if not stat.S_ISDIR(root_stat.st_mode) or stat.S_ISLNK(root_stat.st_mode):
        raise PrivacyError("PRIVACY_ROOT")
    expected = _resolve_public_names(root, contract)
    observed: list[str] = []
    for directory, directories, files in os.walk(root, followlinks=False):
        base = Path(directory)
        for name in directories:
            if stat.S_ISLNK((base / name).lstat().st_mode):
                raise PrivacyError("PRIVACY_FILE_TYPE")
        if directories:
            raise PrivacyError("PRIVACY_FILE_SET")
        for name in files:
            path = base / name
            relative = path.relative_to(root).as_posix()
            if not stat.S_ISREG(path.lstat().st_mode):
                raise PrivacyError("PRIVACY_FILE_TYPE")
            observed.append(relative)
    if tuple(sorted(observed)) != tuple(sorted(expected)):
        raise PrivacyError("PRIVACY_FILE_SET")
    text_names = set(contract.public_text_assets)
    for name in text_names - {"RELEASE-PROVENANCE.json"}:
        text = _read_text(root / name)
        category = _text_category(text)
        if category:
            raise PrivacyError(category)
    payload_names = tuple(name.replace("{tag}", _archive_tag(expected))
                          for name in contract.release_provenance_assets)
    _validate_release_provenance(root / "RELEASE-PROVENANCE.json", root, payload_names)


def _archive_tag(names: tuple[str, ...]) -> str:
    archive = next(name for name in names if name.endswith("-android-arm64.zip"))
    match = re.fullmatch(r"mgba-gba-wifi-link-(v[0-9]+\.[0-9]+\.[0-9]+)-android-arm64\.zip", archive)
    if not match:
        raise PrivacyError("PRIVACY_FILE_SET")
    return match.group(1)
