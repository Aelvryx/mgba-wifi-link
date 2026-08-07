"""Immutable data model for automated release construction."""

from dataclasses import dataclass
import json
from pathlib import Path


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
class BuildEvidence:
    """Resolved, pinned build inputs retained in canonical provenance."""

    runner_image: str
    pinned_actions: tuple[str, ...]
    pinned_toolchains: tuple[str, ...]
    configuration: tuple[tuple[str, str], ...]


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


@dataclass(frozen=True)
class ReleaseContract:
    schema: int
    public_assets: tuple[str, ...]
    archive_members: tuple[str, ...]
    archive_sha256_members: tuple[str, ...]
    build_provenance_siblings: tuple[str, ...]
    release_provenance_assets: tuple[str, ...]
    standalone_sha256_assets: tuple[str, ...]
    public_text_assets: tuple[str, ...]
    file_mode: str
    zip_compression: str
    zip_compression_level: int
    zip_creator: str
    zip_member_order: str


def load_contract(path: Path) -> ReleaseContract:
    """Load the tracked release membership contract as immutable tuples."""
    with path.open(encoding="utf-8") as contract_file:
        data = json.load(contract_file)
    return ReleaseContract(
        schema=data["schema"],
        public_assets=tuple(data["public_assets"]),
        archive_members=tuple(data["archive_members"]),
        archive_sha256_members=tuple(data["archive_sha256_members"]),
        build_provenance_siblings=tuple(data["build_provenance_siblings"]),
        release_provenance_assets=tuple(data["release_provenance_assets"]),
        standalone_sha256_assets=tuple(data["standalone_sha256_assets"]),
        public_text_assets=tuple(data["public_text_assets"]),
        file_mode=data["file_mode"],
        zip_compression=data["zip"]["compression"],
        zip_compression_level=data["zip"]["compression_level"],
        zip_creator=data["zip"]["creator"],
        zip_member_order=data["zip"]["member_order"],
    )
