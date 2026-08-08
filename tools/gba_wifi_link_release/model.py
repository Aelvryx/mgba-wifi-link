"""Immutable data model for automated release construction."""

from dataclasses import dataclass
import json
from pathlib import Path


REQUIRED_BUILD_CONFIGURATION = (
    ("android_abi", "arm64-v8a"),
    ("android_api", "21"),
)


@dataclass(frozen=True)
class GateResult:
    name: str
    workflow: str
    run_id: int
    job_id: int
    conclusion: str


@dataclass(frozen=True)
class ReleaseAsset:
    name: str
    size: int
    sha256: str


@dataclass(frozen=True)
class ActualBuildEvidence:
    """Resolved identity emitted by one clean release build job."""

    role: str
    run_id: int
    job_id: int
    runner_image_os: str
    runner_image_version: str
    ndk_revision: str
    ndk_source_properties_sha256: str
    compiler_sha256: str
    compiler_version: str
    cmake_version: str
    ninja_version: str
    source_commit: str
    source_date_epoch: int
    configuration: tuple[tuple[str, str], ...]
    core: ReleaseAsset
    pinned_actions: tuple[str, ...]


@dataclass(frozen=True)
class BuildEvidence:
    """Resolved, pinned build inputs retained in canonical provenance."""

    runner_image: str
    pinned_actions: tuple[str, ...]
    pinned_toolchains: tuple[str, ...]
    configuration: tuple[tuple[str, str], ...]
    actual_builds: tuple[ActualBuildEvidence, ...] = ()


@dataclass(frozen=True)
class ReleaseContext:
    """Canonical source identity admitted before release construction."""

    repository: str
    tag: str
    tag_object: str
    commit: str
    version: str
    source_date_epoch: int
    prerelease: bool
    gates: tuple[GateResult, ...]
    notes_sha256: str
    build: BuildEvidence | None = None


@dataclass(frozen=True)
class ReleaseSet:
    context: ReleaseContext
    assets: tuple[ReleaseAsset, ...]
    # This is local transport metadata, not part of canonical release bytes. It
    # is populated only by the verifier, so publishing can re-verify and upload
    # the exact immutable files without rebuilding them.
    directory: Path | None = None


@dataclass(frozen=True)
class ReleaseContract:
    schema: int
    public_assets: tuple[str, ...]
    archive_members: tuple[str, ...]
    archive_sha256_members: tuple[str, ...]
    build_provenance_siblings: tuple[str, ...]
    build_configuration: tuple[tuple[str, str], ...]
    release_provenance_assets: tuple[str, ...]
    standalone_sha256_assets: tuple[str, ...]
    public_text_assets: tuple[str, ...]
    public_asset_max_bytes: tuple[tuple[str, int], ...]
    public_aggregate_max_bytes: int
    archive_member_max_bytes: tuple[tuple[str, int], ...]
    archive_central_directory_max_bytes: int
    archive_compressed_aggregate_max_bytes: int
    archive_uncompressed_aggregate_max_bytes: int
    archive_max_compression_ratio: int
    file_mode: str
    license_sha256: str
    zip_compression: str
    zip_compression_level: int
    zip_creator: str
    zip_member_order: str


def load_contract(path: Path) -> ReleaseContract:
    """Load the tracked release membership contract as immutable tuples."""
    with path.open(encoding="utf-8") as contract_file:
        data = json.load(contract_file)
    configuration = tuple(data["build_configuration"].items())
    if configuration != REQUIRED_BUILD_CONFIGURATION:
        raise ValueError("CONTRACT_BUILD_CONFIGURATION")
    resource_limits = data["resource_limits"]
    if (
        not isinstance(resource_limits, dict)
        or set(resource_limits) != {
            "archive_central_directory_max_bytes",
            "archive_compressed_aggregate_max_bytes", "archive_max_compression_ratio",
            "archive_member_max_bytes", "archive_uncompressed_aggregate_max_bytes",
            "public_aggregate_max_bytes", "public_asset_max_bytes",
        }
        or not all(type(value) is int and value > 0 for key, value in resource_limits.items()
                   if not key.endswith("_max_bytes"))
        or not isinstance(resource_limits["public_asset_max_bytes"], dict)
        or not isinstance(resource_limits["archive_member_max_bytes"], dict)
        or any(type(value) is not int or value <= 0
               for value in resource_limits["public_asset_max_bytes"].values())
        or any(type(value) is not int or value <= 0
               for value in resource_limits["archive_member_max_bytes"].values())
        or set(resource_limits["public_asset_max_bytes"]) != set(data["public_assets"])
        or set(resource_limits["archive_member_max_bytes"]) != set(data["archive_members"])
    ):
        raise ValueError("CONTRACT_RESOURCE_LIMITS")
    scalar_limits = (
        "public_aggregate_max_bytes", "archive_central_directory_max_bytes",
        "archive_compressed_aggregate_max_bytes",
        "archive_uncompressed_aggregate_max_bytes", "archive_max_compression_ratio",
    )
    if any(type(resource_limits[name]) is not int or resource_limits[name] <= 0
           for name in scalar_limits):
        raise ValueError("CONTRACT_RESOURCE_LIMITS")
    return ReleaseContract(
        schema=data["schema"],
        public_assets=tuple(data["public_assets"]),
        archive_members=tuple(data["archive_members"]),
        archive_sha256_members=tuple(data["archive_sha256_members"]),
        build_provenance_siblings=tuple(data["build_provenance_siblings"]),
        build_configuration=configuration,
        release_provenance_assets=tuple(data["release_provenance_assets"]),
        standalone_sha256_assets=tuple(data["standalone_sha256_assets"]),
        public_text_assets=tuple(data["public_text_assets"]),
        public_asset_max_bytes=tuple(resource_limits["public_asset_max_bytes"].items()),
        public_aggregate_max_bytes=resource_limits["public_aggregate_max_bytes"],
        archive_member_max_bytes=tuple(resource_limits["archive_member_max_bytes"].items()),
        archive_central_directory_max_bytes=resource_limits["archive_central_directory_max_bytes"],
        archive_compressed_aggregate_max_bytes=resource_limits["archive_compressed_aggregate_max_bytes"],
        archive_uncompressed_aggregate_max_bytes=resource_limits["archive_uncompressed_aggregate_max_bytes"],
        archive_max_compression_ratio=resource_limits["archive_max_compression_ratio"],
        file_mode=data["file_mode"],
        license_sha256=data["license_sha256"],
        zip_compression=data["zip"]["compression"],
        zip_compression_level=data["zip"]["compression_level"],
        zip_creator=data["zip"]["creator"],
        zip_member_order=data["zip"]["member_order"],
    )
