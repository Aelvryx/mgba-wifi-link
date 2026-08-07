"""Canonical, acyclic schema-v1 release provenance."""

import json
from pathlib import Path
import re

from .admission import REQUIRED_GATES, REQUIRED_WORKFLOW
from .model import BuildEvidence, GateResult, ReleaseAsset, ReleaseContext, load_contract


_CONTRACT = (
    Path(__file__).resolve().parents[2]
    / "packaging/gba-wifi-link/release/contract-v1.json"
)
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
_ACTION_PIN_RE = re.compile(r"^[A-Za-z0-9._/-]+@[0-9a-f]{40}$")
_TOOLCHAIN_PIN_RE = re.compile(r"^[A-Za-z0-9._/-]+@[A-Za-z0-9._-]+\+sha256:[0-9a-f]{64}$")


class ProvenanceError(ValueError):
    """A bounded provenance rejection category."""


def canonical_json(value: object) -> bytes:
    """Encode JSON deterministically as compact UTF-8 with one LF."""
    text = json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False)
    return (text + "\n").encode("utf-8")


def _source(context: ReleaseContext) -> dict[str, object]:
    if (
        not isinstance(context.repository, str)
        or not context.repository
        or not isinstance(context.tag, str)
        or not context.tag
        or not isinstance(context.version, str)
        or not context.version
        or not isinstance(context.tag_object, str)
        or not _SHA1_RE.fullmatch(context.tag_object)
        or not isinstance(context.commit, str)
        or not _SHA1_RE.fullmatch(context.commit)
        or type(context.source_date_epoch) is not int
        or context.source_date_epoch < 0
        or type(context.prerelease) is not bool
        or not isinstance(context.notes_sha256, str)
        or not _SHA256_RE.fullmatch(context.notes_sha256)
    ):
        raise ProvenanceError("PROVENANCE_SOURCE")
    return {
        "commit": context.commit,
        "notes_sha256": context.notes_sha256,
        "prerelease": context.prerelease,
        "repository": context.repository,
        "source_date_epoch": context.source_date_epoch,
        "tag": context.tag,
        "tag_object": context.tag_object,
        "version": context.version,
    }


def _gates(gates: tuple[GateResult, ...]) -> list[dict[str, object]]:
    if tuple(gate.name for gate in gates) != REQUIRED_GATES:
        raise ProvenanceError("PROVENANCE_GATE")
    result: list[dict[str, object]] = []
    for gate in gates:
        if (
            gate.workflow != REQUIRED_WORKFLOW
            or type(gate.run_id) is not int
            or gate.run_id <= 0
            or type(gate.job_id) is not int
            or gate.job_id <= 0
            or gate.conclusion != "success"
        ):
            raise ProvenanceError("PROVENANCE_GATE")
        result.append({
            "conclusion": gate.conclusion,
            "job_id": gate.job_id,
            "name": gate.name,
            "run_id": gate.run_id,
            "workflow": gate.workflow,
        })
    return result


def _build(build: BuildEvidence | None) -> dict[str, object]:
    if (
        not isinstance(build, BuildEvidence)
        or not isinstance(build.runner_image, str)
        or not build.runner_image
        or not build.pinned_actions
        or tuple(build.pinned_actions) != tuple(sorted(build.pinned_actions))
        or len(set(build.pinned_actions)) != len(build.pinned_actions)
        or not all(isinstance(action, str) and _ACTION_PIN_RE.fullmatch(action)
                   for action in build.pinned_actions)
        or not build.pinned_toolchains
        or tuple(build.pinned_toolchains) != tuple(sorted(build.pinned_toolchains))
        or len(set(build.pinned_toolchains)) != len(build.pinned_toolchains)
        or not all(isinstance(toolchain, str) and _TOOLCHAIN_PIN_RE.fullmatch(toolchain)
                   for toolchain in build.pinned_toolchains)
        or not build.configuration
        or tuple(key for key, _ in build.configuration) != tuple(sorted(key for key, _ in build.configuration))
        or len({key for key, _ in build.configuration}) != len(build.configuration)
        or any(not isinstance(key, str) or not key or not isinstance(value, str) or not value
               for key, value in build.configuration)
    ):
        raise ProvenanceError("PROVENANCE_BUILD")
    return {
        "configuration": dict(build.configuration),
        "pinned_actions": list(build.pinned_actions),
        "pinned_toolchains": list(build.pinned_toolchains),
        "runner_image": build.runner_image,
    }


def _assets(assets: tuple[ReleaseAsset, ...], expected_names: tuple[str, ...]) -> list[dict[str, object]]:
    if tuple(asset.name for asset in assets) != expected_names:
        raise ProvenanceError("PROVENANCE_OWNERSHIP")
    result: list[dict[str, object]] = []
    for asset in assets:
        if (
            type(asset.size) is not int
            or asset.size < 0
            or not isinstance(asset.sha256, str)
            or not _SHA256_RE.fullmatch(asset.sha256)
        ):
            raise ProvenanceError("PROVENANCE_ASSET")
        result.append({"name": asset.name, "sha256": asset.sha256, "size": asset.size})
    return result


def build_provenance(context: ReleaseContext, siblings: tuple[ReleaseAsset, ...]) -> bytes:
    """Build archive provenance before its checksum and enclosing ZIP exist."""
    contract = load_contract(_CONTRACT)
    return canonical_json({
        "build": _build(context.build),
        "gates": _gates(context.gates),
        "schema": contract.schema,
        "siblings": _assets(siblings, contract.build_provenance_siblings),
        "source": _source(context),
    })


def release_provenance(context: ReleaseContext, payloads: tuple[ReleaseAsset, ...]) -> bytes:
    """Describe exactly the five payload assets after archive construction."""
    contract = load_contract(_CONTRACT)
    expected_names = tuple(name.replace("{tag}", context.tag)
                           for name in contract.release_provenance_assets)
    return canonical_json({
        "build": _build(context.build),
        "payloads": _assets(payloads, expected_names),
        "schema": contract.schema,
        "source": _source(context),
    })
