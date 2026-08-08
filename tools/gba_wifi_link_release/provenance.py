"""Canonical, acyclic schema-v1 release provenance."""

from dataclasses import replace
import json
from pathlib import Path
import re

from .admission import REQUIRED_GATES, REQUIRED_WORKFLOW
from .model import (ActualBuildEvidence, BuildEvidence, GateResult,
                    REQUIRED_BUILD_CONFIGURATION, ReleaseAsset, ReleaseContext,
                    load_contract)


_CONTRACT = (
    Path(__file__).resolve().parents[2]
    / "packaging/gba-wifi-link/release/contract-v1.json"
)
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
_ACTION_PIN_RE = re.compile(r"^[A-Za-z0-9._/-]+@[0-9a-f]{40}$")
_ACTION_IDENTITY_RE = re.compile(
    r"^(?P<name>[A-Za-z0-9._/-]+)@(?P<version>v[0-9]+(?:\.[0-9]+){0,2})"
    r"\+sha:(?P<sha>[0-9a-f]{40})$"
)
_TOOLCHAIN_PIN_RE = re.compile(r"^[A-Za-z0-9._/-]+@[A-Za-z0-9._-]+\+sha256:[0-9a-f]{64}$")
_ACTION_VERSIONS = {
    "actions/checkout": "v6",
    "actions/download-artifact": "v5",
    "actions/upload-artifact": "v4",
}


class ProvenanceError(ValueError):
    """A bounded provenance rejection category."""


def bind_actual_builds(context: ReleaseContext,
                       identities: tuple[ActualBuildEvidence, ...]) -> ReleaseContext:
    """Bind two compared clean-build identities to an admitted context."""
    if not isinstance(context.build, BuildEvidence) or len(identities) != 2:
        raise ProvenanceError("PROVENANCE_BUILD")
    build = replace(context.build, actual_builds=identities)
    bound = replace(context, build=build)
    _build(build, bound, identities[0].core)
    return bound


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
            "name": gate.name,
            "workflow": gate.workflow,
        })
    return result


def _actual_build(identity: ActualBuildEvidence, context: ReleaseContext,
                  expected_core: ReleaseAsset, planned_actions: tuple[str, ...],
                  planned_toolchains: tuple[str, ...]) -> dict[str, object]:
    action_matches = tuple(
        _ACTION_IDENTITY_RE.fullmatch(action)
        if isinstance(action, str) else None
        for action in identity.pinned_actions
    )
    required_actions = {
        "protected": {"actions/checkout", "actions/download-artifact", "actions/upload-artifact"},
        "independent": {"actions/checkout", "actions/upload-artifact"},
    }
    if (
        not isinstance(identity, ActualBuildEvidence)
        or identity.role not in required_actions
        or type(identity.run_id) is not int
        or identity.run_id <= 0
        or type(identity.job_id) is not int
        or identity.job_id <= 0
        or any(not isinstance(value, str) or not value or "\n" in value or "\r" in value for value in (
            identity.runner_image_os, identity.runner_image_version, identity.ndk_revision,
            identity.compiler_version, identity.cmake_version, identity.ninja_version,
        ))
        or not _SHA256_RE.fullmatch(identity.ndk_source_properties_sha256)
        or not _SHA256_RE.fullmatch(identity.compiler_sha256)
        or identity.source_commit != context.commit
        or identity.source_date_epoch != context.source_date_epoch
        or identity.configuration != REQUIRED_BUILD_CONFIGURATION
        or identity.core != expected_core
        or not identity.pinned_actions
        or tuple(identity.pinned_actions) != tuple(sorted(identity.pinned_actions))
        or len(set(identity.pinned_actions)) != len(identity.pinned_actions)
        or any(match is None for match in action_matches)
    ):
        raise ProvenanceError("PROVENANCE_BUILD")
    matches = tuple(match for match in action_matches if match is not None)
    if (
        {match.group("name") for match in matches} != required_actions[identity.role]
        or any(_ACTION_VERSIONS.get(match.group("name")) != match.group("version")
               for match in matches)
        or any(f'{match.group("name")}@{match.group("sha")}' not in planned_actions
               for match in matches)
        or (
            f"android-ndk@{identity.ndk_revision}+sha256:"
            f"{identity.ndk_source_properties_sha256}"
        ) not in planned_toolchains
    ):
        raise ProvenanceError("PROVENANCE_BUILD")
    return {
        "cmake_version": identity.cmake_version,
        "compiler_sha256": identity.compiler_sha256,
        "compiler_version": identity.compiler_version,
        "configuration": dict(identity.configuration),
        "core": {"name": identity.core.name, "sha256": identity.core.sha256,
                 "size": identity.core.size},
        "ndk_revision": identity.ndk_revision,
        "ndk_source_properties_sha256": identity.ndk_source_properties_sha256,
        "ninja_version": identity.ninja_version,
        "pinned_actions": list(identity.pinned_actions),
        "role": identity.role,
        "runner_image_os": identity.runner_image_os,
        "runner_image_version": identity.runner_image_version,
        "source_commit": identity.source_commit,
        "source_date_epoch": identity.source_date_epoch,
    }


def _build(build: BuildEvidence | None, context: ReleaseContext,
           expected_core: ReleaseAsset) -> dict[str, object]:
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
        or build.configuration != REQUIRED_BUILD_CONFIGURATION
        or len(build.actual_builds) != 2
        or not all(isinstance(identity, ActualBuildEvidence)
                   for identity in build.actual_builds)
    ):
        raise ProvenanceError("PROVENANCE_BUILD")
    if tuple(identity.role for identity in build.actual_builds) != ("protected", "independent"):
        raise ProvenanceError("PROVENANCE_BUILD")
    actual = tuple(
        _actual_build(identity, context, expected_core, build.pinned_actions,
                      build.pinned_toolchains)
        for identity in build.actual_builds
    )
    first, second = build.actual_builds
    reproducible_first = (
        first.runner_image_os, first.runner_image_version, first.ndk_revision,
        first.ndk_source_properties_sha256, first.compiler_sha256, first.compiler_version,
        first.cmake_version, first.ninja_version, first.source_commit,
        first.source_date_epoch, first.configuration, first.core,
    )
    reproducible_second = (
        second.runner_image_os, second.runner_image_version, second.ndk_revision,
        second.ndk_source_properties_sha256, second.compiler_sha256, second.compiler_version,
        second.cmake_version, second.ninja_version, second.source_commit,
        second.source_date_epoch, second.configuration, second.core,
    )
    if (
        reproducible_first != reproducible_second
        or first.run_id != second.run_id
        or first.job_id == second.job_id
    ):
        raise ProvenanceError("PROVENANCE_BUILD")
    return {
        "actual_builds": list(actual),
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
    core = next((asset for asset in siblings if asset.name == "mgba_libretro_android.so"), None)
    if core is None:
        raise ProvenanceError("PROVENANCE_BUILD")
    validated_siblings = _assets(siblings, contract.build_provenance_siblings)
    return canonical_json({
        "build": _build(context.build, context, core),
        "gates": _gates(context.gates),
        "schema": contract.schema,
        "siblings": validated_siblings,
        "source": _source(context),
    })


def release_provenance(context: ReleaseContext, payloads: tuple[ReleaseAsset, ...]) -> bytes:
    """Describe exactly the five payload assets after archive construction."""
    contract = load_contract(_CONTRACT)
    expected_names = tuple(name.replace("{tag}", context.tag)
                           for name in contract.release_provenance_assets)
    core = next((asset for asset in payloads if asset.name == "mgba_libretro_android.so"), None)
    if core is None:
        raise ProvenanceError("PROVENANCE_BUILD")
    validated_payloads = _assets(payloads, expected_names)
    return canonical_json({
        "build": _build(context.build, context, core),
        "payloads": validated_payloads,
        "schema": contract.schema,
        "source": _source(context),
    })
