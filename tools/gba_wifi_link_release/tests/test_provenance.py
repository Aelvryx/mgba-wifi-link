"""Canonical schema-v1 provenance serialization tests."""

from dataclasses import replace
import json
import unittest

from tools.gba_wifi_link_release.admission import REQUIRED_GATES, REQUIRED_WORKFLOW
from tools.gba_wifi_link_release.model import BuildEvidence, GateResult, ReleaseAsset, ReleaseContext
from tools.gba_wifi_link_release.provenance import (
    ProvenanceError,
    build_provenance,
    canonical_json,
    release_provenance,
)


BUILD_EVIDENCE = BuildEvidence(
    runner_image="ubuntu-24.04-20260125.1",
    pinned_actions=(
        "actions/checkout@0123456789abcdef0123456789abcdef01234567",
        "actions/upload-artifact@89abcdef0123456789abcdef0123456789abcdef",
    ),
    pinned_toolchains=("android-ndk@27.2.12479018+sha256:" + "d" * 64,),
    configuration=(("android_abi", "arm64-v8a"), ("android_api", "21")),
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
    ReleaseAsset("mgba_libretro_android.so", 15, "5" * 64),
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
                "pinned_actions": [
                    "actions/checkout@0123456789abcdef0123456789abcdef01234567",
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
