"""Canonical schema-v1 provenance serialization tests."""

from dataclasses import replace
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.gba_wifi_link_release.admission import REQUIRED_GATES, REQUIRED_WORKFLOW
from tools.gba_wifi_link_release.model import (
    ActualBuildEvidence,
    BuildEvidence,
    GateResult,
    ReleaseAsset,
    ReleaseContext,
)
from tools.gba_wifi_link_release.provenance import (
    ProvenanceError,
    build_provenance,
    canonical_json,
    release_provenance,
)


ROOT = Path(__file__).resolve().parents[3]
CLI = ROOT / "tools/gba-wifi-link-release.py"


CORE_EVIDENCE = ReleaseAsset("mgba_libretro_android.so", 15, "5" * 64)
ACTUAL_BUILDS = (
    ActualBuildEvidence(
        role="protected",
        run_id=900,
        job_id=901,
        runner_image_os="ubuntu24",
        runner_image_version="20260125.1",
        ndk_revision="27.2.12479018",
        ndk_source_properties_sha256="d" * 64,
        compiler_sha256="e" * 64,
        compiler_version="Android clang version 18.0.3",
        cmake_version="cmake version 3.31.6",
        ninja_version="1.12.1",
        source_commit="b" * 40,
        source_date_epoch=1_700_000_000,
        configuration=(("android_abi", "arm64-v8a"), ("android_api", "21")),
        core=CORE_EVIDENCE,
        pinned_actions=(
            "actions/checkout@v6+sha:0123456789abcdef0123456789abcdef01234567",
            "actions/download-artifact@v5+sha:" + "1" * 40,
            "actions/upload-artifact@v4+sha:89abcdef0123456789abcdef0123456789abcdef",
        ),
    ),
    ActualBuildEvidence(
        role="independent",
        run_id=900,
        job_id=902,
        runner_image_os="ubuntu24",
        runner_image_version="20260125.1",
        ndk_revision="27.2.12479018",
        ndk_source_properties_sha256="d" * 64,
        compiler_sha256="e" * 64,
        compiler_version="Android clang version 18.0.3",
        cmake_version="cmake version 3.31.6",
        ninja_version="1.12.1",
        source_commit="b" * 40,
        source_date_epoch=1_700_000_000,
        configuration=(("android_abi", "arm64-v8a"), ("android_api", "21")),
        core=CORE_EVIDENCE,
        pinned_actions=(
            "actions/checkout@v6+sha:0123456789abcdef0123456789abcdef01234567",
            "actions/upload-artifact@v4+sha:89abcdef0123456789abcdef0123456789abcdef",
        ),
    ),
)


def actual_build_dict(build: ActualBuildEvidence) -> dict[str, object]:
    return {
        "cmake_version": build.cmake_version,
        "compiler_sha256": build.compiler_sha256,
        "compiler_version": build.compiler_version,
        "configuration": dict(build.configuration),
        "core": {"name": build.core.name, "sha256": build.core.sha256,
                 "size": build.core.size},
        "job_id": build.job_id,
        "ndk_revision": build.ndk_revision,
        "ndk_source_properties_sha256": build.ndk_source_properties_sha256,
        "ninja_version": build.ninja_version,
        "pinned_actions": list(build.pinned_actions),
        "role": build.role,
        "run_id": build.run_id,
        "runner_image_os": build.runner_image_os,
        "runner_image_version": build.runner_image_version,
        "source_commit": build.source_commit,
        "source_date_epoch": build.source_date_epoch,
    }

BUILD_EVIDENCE = BuildEvidence(
    runner_image="ubuntu-24.04-20260125.1",
    pinned_actions=(
        "actions/checkout@0123456789abcdef0123456789abcdef01234567",
        "actions/download-artifact@" + "1" * 40,
        "actions/upload-artifact@89abcdef0123456789abcdef0123456789abcdef",
    ),
    pinned_toolchains=("android-ndk@27.2.12479018+sha256:" + "d" * 64,),
    configuration=(("android_abi", "arm64-v8a"), ("android_api", "21")),
    actual_builds=ACTUAL_BUILDS,
)

CONTEXT = ReleaseContext(
    repository="Aelvryx/mgba-wifi-link",
    tag="v9.8.7",
    tag_object="a" * 40,
    commit="b" * 40,
    version="9.8.7",
    source_date_epoch=1_700_000_000,
    prerelease=True,
    gates=tuple(
        GateResult(name, REQUIRED_WORKFLOW, index + 100, index + 200, "success")
        for index, name in enumerate(REQUIRED_GATES)
    ),
    notes_sha256="c" * 64,
    build=BUILD_EVIDENCE,
)

BUILD_SIBLINGS = (
    ReleaseAsset("INSTALL-AND-USAGE.md", 10, "0" * 64),
    ReleaseAsset("LICENSE", 11, "1" * 64),
    ReleaseAsset("SOURCE-AND-PROVENANCE.md", 12, "2" * 64),
    ReleaseAsset("gba-link-continuous.gba", 13, "3" * 64),
    ReleaseAsset("gba-link-test.gba", 14, "4" * 64),
    CORE_EVIDENCE,
)
RELEASE_PAYLOADS = (
    ReleaseAsset("mgba_libretro_android.so", 15, "5" * 64),
    ReleaseAsset("gba-link-test.gba", 14, "4" * 64),
    ReleaseAsset("gba-link-continuous.gba", 13, "3" * 64),
    ReleaseAsset("INSTALL-AND-USAGE.md", 10, "0" * 64),
    ReleaseAsset("mgba-gba-wifi-link-v9.8.7-android-arm64.zip", 16, "6" * 64),
)


class ProvenanceTest(unittest.TestCase):
    def test_canonical_json_is_sorted_compact_utf8_lf(self):
        self.assertEqual(
            canonical_json({"z": 1, "a": "é"}),
            b'{"a":"\xc3\xa9","z":1}\n',
        )

    def test_build_provenance_has_schema_one_exact_source_gate_and_sibling_fields(self):
        document = json.loads(build_provenance(CONTEXT, BUILD_SIBLINGS))

        self.assertEqual(tuple(document), ("build", "gates", "schema", "siblings", "source"))
        self.assertEqual(document["schema"], 1)
        self.assertEqual(
            document["build"],
            {
                "configuration": {"android_abi": "arm64-v8a", "android_api": "21"},
                "actual_builds": [
                    {
                        "cmake_version": build.cmake_version,
                        "compiler_sha256": build.compiler_sha256,
                        "compiler_version": build.compiler_version,
                        "configuration": dict(build.configuration),
                        "core": {"name": build.core.name, "sha256": build.core.sha256,
                                 "size": build.core.size},
                        "job_id": build.job_id,
                        "ndk_revision": build.ndk_revision,
                        "ndk_source_properties_sha256": build.ndk_source_properties_sha256,
                        "ninja_version": build.ninja_version,
                        "pinned_actions": list(build.pinned_actions),
                        "role": build.role,
                        "run_id": build.run_id,
                        "runner_image_os": build.runner_image_os,
                        "runner_image_version": build.runner_image_version,
                        "source_commit": build.source_commit,
                        "source_date_epoch": build.source_date_epoch,
                    }
                    for build in ACTUAL_BUILDS
                ],
                "pinned_actions": [
                    "actions/checkout@0123456789abcdef0123456789abcdef01234567",
                    "actions/download-artifact@" + "1" * 40,
                    "actions/upload-artifact@89abcdef0123456789abcdef0123456789abcdef",
                ],
                "pinned_toolchains": ["android-ndk@27.2.12479018+sha256:" + "d" * 64],
                "runner_image": "ubuntu-24.04-20260125.1",
            },
        )
        self.assertEqual(
            document["source"],
            {
                "commit": "b" * 40,
                "notes_sha256": "c" * 64,
                "prerelease": True,
                "repository": "Aelvryx/mgba-wifi-link",
                "source_date_epoch": 1_700_000_000,
                "tag": "v9.8.7",
                "tag_object": "a" * 40,
                "version": "9.8.7",
            },
        )
        self.assertEqual(tuple(gate["name"] for gate in document["gates"]), REQUIRED_GATES)
        self.assertTrue(all(gate["workflow"] == REQUIRED_WORKFLOW for gate in document["gates"]))
        self.assertEqual(
            [asset["name"] for asset in document["siblings"]],
            [asset.name for asset in BUILD_SIBLINGS],
        )
        self.assertNotIn("mgba-gba-wifi-link-v9.8.7-android-arm64.zip", str(document))
        self.assertNotIn("BUILD-PROVENANCE.json", str(document))
        self.assertNotIn("SHA256SUMS", str(document))

    def test_release_provenance_has_only_five_declared_payload_hashes(self):
        document = json.loads(release_provenance(CONTEXT, RELEASE_PAYLOADS))

        self.assertEqual(tuple(document), ("build", "payloads", "schema", "source"))
        self.assertEqual(document["schema"], 1)
        self.assertEqual(document["build"]["runner_image"], "ubuntu-24.04-20260125.1")
        self.assertEqual(document["source"]["commit"], "b" * 40)
        self.assertEqual(len(document["payloads"]), 5)
        self.assertEqual(
            [asset["name"] for asset in document["payloads"]],
            [asset.name for asset in RELEASE_PAYLOADS],
        )
        self.assertEqual(
            [asset["sha256"] for asset in document["payloads"]],
            [asset.sha256 for asset in RELEASE_PAYLOADS],
        )
        self.assertNotIn("RELEASE-PROVENANCE.json", str(document))
        self.assertNotIn("SHA256SUMS", str(document))

    def test_provenance_rejects_planned_inputs_without_two_actual_builds(self):
        missing = replace(BUILD_EVIDENCE, actual_builds=())
        with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_BUILD$"):
            build_provenance(replace(CONTEXT, build=missing), BUILD_SIBLINGS)
        malformed = replace(BUILD_EVIDENCE, actual_builds=(ACTUAL_BUILDS[0], None))
        with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_BUILD$"):
            build_provenance(replace(CONTEXT, build=malformed), BUILD_SIBLINGS)  # type: ignore[arg-type]

    def test_provenance_rejects_mismatched_actual_builds_and_action_versions(self):
        second = ACTUAL_BUILDS[1]
        mutations = (
            replace(second, runner_image_version="20260126.1"),
            replace(second, compiler_sha256="f" * 64),
            replace(second, cmake_version="cmake version 4.0.0"),
            replace(second, source_commit="c" * 40),
            replace(second, source_date_epoch=1_700_000_001),
            replace(second, core=replace(second.core, sha256="6" * 64)),
            replace(second, pinned_actions=("actions/checkout@" + "0" * 40,)),
            replace(second, pinned_actions=(
                "actions/checkout@v9+sha:0123456789abcdef0123456789abcdef01234567",
                "actions/upload-artifact@v4+sha:89abcdef0123456789abcdef0123456789abcdef",
            )),
            replace(second, run_id=901),
            replace(second, job_id=901),
        )
        for mutated in mutations:
            with self.subTest(mutated=mutated):
                build = replace(BUILD_EVIDENCE, actual_builds=(ACTUAL_BUILDS[0], mutated))
                with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_BUILD$"):
                    build_provenance(replace(CONTEXT, build=build), BUILD_SIBLINGS)
        wrong_ndk = tuple(replace(build, ndk_revision="27.3.0") for build in ACTUAL_BUILDS)
        with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_BUILD$"):
            build_provenance(
                replace(CONTEXT, build=replace(BUILD_EVIDENCE, actual_builds=wrong_ndk)),
                BUILD_SIBLINGS,
            )

    def test_cli_binds_two_actual_builds_deterministically_and_rejects_mismatch(self):
        from tools.gba_wifi_link_release.cli import _context_dict

        admitted = replace(CONTEXT, build=replace(BUILD_EVIDENCE, actual_builds=()))
        identities = {build.role: actual_build_dict(build) for build in ACTUAL_BUILDS}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            context_path = root / "context.json"
            identities_path = root / "identities.json"
            context_path.write_text(json.dumps(_context_dict(admitted)), encoding="utf-8")
            identities_path.write_text(
                json.dumps(identities, sort_keys=True, separators=(",", ":")) + "\n",
                encoding="utf-8",
            )
            command = (
                sys.executable, str(CLI), "bind-builds", "--context", str(context_path),
                "--identities", str(identities_path),
            )

            first = subprocess.run(command, check=False, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
            second = subprocess.run(command, check=False, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)

            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(first.stdout, second.stdout)
            bound = json.loads(first.stdout)
            self.assertEqual(
                [build["role"] for build in bound["build"]["actual_builds"]],
                ["protected", "independent"],
            )

            invalid_documents = (
                json.dumps({"protected": identities["protected"]}, sort_keys=True),
                json.dumps({**identities, "extra": {}}, sort_keys=True),
                "{" +
                f'"independent":{json.dumps(identities["independent"], sort_keys=True)},' +
                f'"protected":{json.dumps(identities["protected"], sort_keys=True)},' +
                f'"protected":{json.dumps(identities["protected"], sort_keys=True)}' +
                "}",
            )
            for document in invalid_documents:
                with self.subTest(document=document[:40]):
                    identities_path.write_text(document + "\n", encoding="utf-8")
                    invalid = subprocess.run(command, check=False, stdout=subprocess.PIPE,
                                             stderr=subprocess.PIPE)
                    self.assertEqual(invalid.returncode, 2)
                    self.assertEqual(invalid.stderr, b"CLI_BUILD_IDENTITIES\n")

            identities["independent"]["compiler_sha256"] = "f" * 64  # type: ignore[index]
            identities_path.write_text(json.dumps(identities), encoding="utf-8")
            mismatch = subprocess.run(command, check=False, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE)
            self.assertEqual(mismatch.returncode, 2)
            self.assertEqual(mismatch.stderr, b"PROVENANCE_BUILD\n")

    def test_provenance_rejects_invalid_assets_and_gate_evidence(self):
        invalid_hash = replace(BUILD_SIBLINGS[0], sha256="A" * 64)
        with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_ASSET$"):
            build_provenance(CONTEXT, (invalid_hash, *BUILD_SIBLINGS[1:]))
        with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_ASSET$"):
            build_provenance(
                CONTEXT,
                (replace(BUILD_SIBLINGS[0], sha256=123), *BUILD_SIBLINGS[1:]),  # type: ignore[arg-type]
            )
        invalid_size = replace(RELEASE_PAYLOADS[0], size=-1)
        with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_ASSET$"):
            release_provenance(CONTEXT, (invalid_size, *RELEASE_PAYLOADS[1:]))
        invalid_gate = replace(CONTEXT.gates[0], run_id=True)
        with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_GATE$"):
            build_provenance(replace(CONTEXT, gates=(invalid_gate, *CONTEXT.gates[1:])), BUILD_SIBLINGS)
        with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_SOURCE$"):
            build_provenance(replace(CONTEXT, notes_sha256=123), BUILD_SIBLINGS)  # type: ignore[arg-type]
        with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_BUILD$"):
            build_provenance(replace(CONTEXT, build=None), BUILD_SIBLINGS)  # type: ignore[arg-type]

    def test_provenance_rejects_unpinned_duplicate_unordered_and_unknown_build_evidence(self):
        invalid_builds = (
            replace(BUILD_EVIDENCE, pinned_actions=("actions/checkout@v4",)),
            replace(BUILD_EVIDENCE, pinned_actions=(
                "actions/checkout@0123456789abcdef0123456789abcdef01234567",
                "actions/checkout@0123456789abcdef0123456789abcdef01234567",
            )),
            replace(BUILD_EVIDENCE, configuration=(
                ("android_api", "21"), ("android_abi", "arm64-v8a"),
            )),
            replace(BUILD_EVIDENCE, configuration=(("unexpected", "value"),)),
        )
        for build in invalid_builds:
            with self.subTest(build=build):
                with self.assertRaisesRegex(ProvenanceError, "^PROVENANCE_BUILD$"):
                    build_provenance(replace(CONTEXT, build=build), BUILD_SIBLINGS)

    def test_provenance_rejects_cycles_and_wrong_declared_membership(self):
        self.assertRaisesRegex(
            ProvenanceError,
            "^PROVENANCE_OWNERSHIP$",
            build_provenance,
            CONTEXT,
            (*BUILD_SIBLINGS, ReleaseAsset("BUILD-PROVENANCE.json", 1, "f" * 64)),
        )
        self.assertRaisesRegex(
            ProvenanceError,
            "^PROVENANCE_OWNERSHIP$",
            release_provenance,
            CONTEXT,
            (*RELEASE_PAYLOADS[:-1], ReleaseAsset("RELEASE-PROVENANCE.json", 1, "f" * 64)),
        )
