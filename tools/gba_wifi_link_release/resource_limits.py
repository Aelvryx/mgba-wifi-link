"""Contract-owned bounds for remote bytes, JSON, and ZIP expansion."""

import json
import os
from pathlib import Path
import re
import stat
import struct
from typing import Iterable, Mapping
import zipfile

from .model import ReleaseContract, load_contract


CONTRACT_PATH = (
    Path(__file__).resolve().parents[2]
    / "packaging/gba-wifi-link/release/contract-v1.json"
)


class ResourceLimitError(ValueError):
    """A bounded input exceeded the release contract's resource budget."""


def _contract(contract: ReleaseContract | None) -> ReleaseContract:
    return contract if contract is not None else load_contract(CONTRACT_PATH)


def _public_template(name: str, contract: ReleaseContract) -> str | None:
    for template, _ in contract.public_asset_max_bytes:
        if template == name:
            return template
        if "{tag}" in template:
            pattern = re.escape(template).replace(
                re.escape("{tag}"), r"v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)",
            )
            if re.fullmatch(pattern, name):
                return template
    return None


def public_asset_max_bytes(name: str,
                           contract: ReleaseContract | None = None) -> int:
    selected = _contract(contract)
    template = _public_template(name, selected)
    if template is None:
        raise ResourceLimitError("RESOURCE_PUBLIC_NAME")
    return dict(selected.public_asset_max_bytes)[template]


def validate_public_asset_sizes(
    assets: Iterable[tuple[str, int]],
    contract: ReleaseContract | None = None,
) -> None:
    selected = _contract(contract)
    total = 0
    names: set[str] = set()
    for name, size in assets:
        if (
            not isinstance(name, str)
            or name in names
            or type(size) is not int
            or size < 0
            or size > public_asset_max_bytes(name, selected)
        ):
            raise ResourceLimitError("RESOURCE_PUBLIC_SIZE")
        names.add(name)
        total += size
        if total > selected.public_aggregate_max_bytes:
            raise ResourceLimitError("RESOURCE_PUBLIC_AGGREGATE")


def read_bounded_regular(path: Path, maximum_bytes: int) -> bytes:
    """Read one no-follow regular file without crossing its declared ceiling."""
    if type(maximum_bytes) is not int or maximum_bytes < 0 or not hasattr(os, "O_NOFOLLOW"):
        raise ResourceLimitError("RESOURCE_FILE")
    flags = os.O_RDONLY | os.O_NOFOLLOW | getattr(os, "O_CLOEXEC", 0)
    descriptor: int | None = None
    try:
        descriptor = os.open(path, flags)
        status = os.fstat(descriptor)
        if not stat.S_ISREG(status.st_mode) or status.st_size > maximum_bytes:
            raise ResourceLimitError("RESOURCE_FILE_SIZE")
        data = bytearray()
        while chunk := os.read(descriptor, min(1 << 16, maximum_bytes + 1 - len(data))):
            data.extend(chunk)
            if len(data) > maximum_bytes:
                raise ResourceLimitError("RESOURCE_FILE_SIZE")
        if len(data) != status.st_size:
            raise ResourceLimitError("RESOURCE_FILE_CHANGED")
        return bytes(data)
    except ResourceLimitError:
        raise
    except OSError as error:
        raise ResourceLimitError("RESOURCE_FILE") from error
    finally:
        if descriptor is not None:
            os.close(descriptor)


def preflight_zip_container(
    data: bytes,
    contract: ReleaseContract | None = None,
) -> None:
    """Reject impossible or oversized ZIP metadata before ``zipfile`` parses it."""
    selected = _contract(contract)
    if not isinstance(data, bytes) or len(data) < 22:
        raise ResourceLimitError("RESOURCE_ARCHIVE_CONTAINER")
    eocd = data.rfind(b"PK\x05\x06", max(0, len(data) - 65_557))
    if eocd < 0 or eocd + 22 > len(data):
        raise ResourceLimitError("RESOURCE_ARCHIVE_CONTAINER")
    try:
        (
            disk, central_disk, disk_entries, total_entries,
            central_size, central_offset, comment_size,
        ) = struct.unpack_from("<HHHHIIH", data, eocd + 4)
    except struct.error as error:
        raise ResourceLimitError("RESOURCE_ARCHIVE_CONTAINER") from error
    if (
        disk != 0
        or central_disk != 0
        or disk_entries != total_entries
        or total_entries != len(selected.archive_members)
        or central_size > selected.archive_central_directory_max_bytes
        or central_offset > eocd
        or central_size > eocd - central_offset
        or central_offset + central_size != eocd
        or eocd + 22 + comment_size != len(data)
    ):
        raise ResourceLimitError("RESOURCE_ARCHIVE_CONTAINER")


def preflight_zip_infos(
    infos: Iterable[zipfile.ZipInfo],
    contract: ReleaseContract | None = None,
) -> None:
    selected = _contract(contract)
    member_limits = dict(selected.archive_member_max_bytes)
    names: set[str] = set()
    compressed_total = 0
    uncompressed_total = 0
    for info in infos:
        if info.filename in names or info.filename not in member_limits:
            raise ResourceLimitError("RESOURCE_ARCHIVE_NAME")
        names.add(info.filename)
        compressed = info.compress_size
        uncompressed = info.file_size
        maximum = member_limits[info.filename]
        if (
            type(compressed) is not int
            or type(uncompressed) is not int
            or compressed < 0
            or uncompressed < 0
            or compressed > maximum
            or uncompressed > maximum
            or (uncompressed > 0 and compressed == 0)
            or uncompressed > compressed * selected.archive_max_compression_ratio
        ):
            raise ResourceLimitError("RESOURCE_ARCHIVE_MEMBER")
        compressed_total += compressed
        uncompressed_total += uncompressed
        if compressed_total > selected.archive_compressed_aggregate_max_bytes:
            raise ResourceLimitError("RESOURCE_ARCHIVE_COMPRESSED")
        if uncompressed_total > selected.archive_uncompressed_aggregate_max_bytes:
            raise ResourceLimitError("RESOURCE_ARCHIVE_UNCOMPRESSED")


def _preflight_json_depth(data: bytes, maximum: int) -> None:
    depth = 0
    quoted = False
    escaped = False
    for byte in data:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 0x5C:
                escaped = True
            elif byte == 0x22:
                quoted = False
        elif byte == 0x22:
            quoted = True
        elif byte in (0x5B, 0x7B):
            depth += 1
            if depth > maximum:
                raise ResourceLimitError("RESOURCE_JSON_DEPTH")
        elif byte in (0x5D, 0x7D):
            depth -= 1
            if depth < 0:
                raise ResourceLimitError("RESOURCE_JSON")
    if quoted or depth != 0:
        raise ResourceLimitError("RESOURCE_JSON")


def _unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    value: dict[str, object] = {}
    for key, item in pairs:
        if key in value:
            raise ResourceLimitError("RESOURCE_JSON_DUPLICATE")
        value[key] = item
    return value


def _check_json_nodes(value: object, maximum_depth: int,
                      maximum_nodes: int) -> None:
    nodes = 0
    pending = [(value, 0)]
    while pending:
        current, depth = pending.pop()
        nodes += 1
        if nodes > maximum_nodes:
            raise ResourceLimitError("RESOURCE_JSON_NODES")
        if depth > maximum_depth:
            raise ResourceLimitError("RESOURCE_JSON_DEPTH")
        if isinstance(current, Mapping):
            pending.extend((child, depth + 1) for child in current.values())
        elif isinstance(current, list):
            pending.extend((child, depth + 1) for child in current)


def bounded_canonical_json(
    data: bytes,
    *,
    required: set[str],
    contract: ReleaseContract | None = None,
) -> dict[str, object]:
    selected = _contract(contract)
    if not isinstance(data, bytes) or len(data) > selected.json_max_bytes:
        raise ResourceLimitError("RESOURCE_JSON_SIZE")
    _preflight_json_depth(data, selected.json_max_depth)
    try:
        value = json.loads(
            data.decode("utf-8"), object_pairs_hook=_unique_object,
            parse_constant=lambda _: (_ for _ in ()).throw(ValueError()),
        )
    except ResourceLimitError:
        raise
    except (UnicodeDecodeError, ValueError, json.JSONDecodeError, RecursionError) as error:
        raise ResourceLimitError("RESOURCE_JSON") from error
    _check_json_nodes(value, selected.json_max_depth, selected.json_max_nodes)
    if not isinstance(value, dict) or set(value) != required:
        raise ResourceLimitError("RESOURCE_JSON_FIELDS")
    try:
        canonical = (json.dumps(
            value, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
            allow_nan=False,
        ) + "\n").encode("utf-8")
    except (TypeError, ValueError, UnicodeEncodeError) as error:
        raise ResourceLimitError("RESOURCE_JSON") from error
    if canonical != data:
        raise ResourceLimitError("RESOURCE_JSON_CANONICAL")
    return value
