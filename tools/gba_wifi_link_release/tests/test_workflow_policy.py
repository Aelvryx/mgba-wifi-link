"""Static contracts for the protected GBA Wi-Fi Link validation workflow."""

import json
from pathlib import Path
import shutil
import tempfile
import unittest

from tools.gba_wifi_link_release.workflow_policy import (
    WorkflowPolicyError,
    lex_workflow_yaml,
    normalize_workflow,
    validate_release_workflow_policy,
    validate_workflow_policy,
)


ROOT = Path(__file__).resolve().parents[3]
HISTORY = ROOT / "tools/gba_wifi_link_release/fixtures/v0.2.0-history.json"


class WorkflowPolicyTest(unittest.TestCase):
    def copy_policy_repo(self) -> tempfile.TemporaryDirectory[str]:
        temporary = tempfile.TemporaryDirectory()
        repo = Path(temporary.name)
        shutil.copytree(ROOT / ".github", repo / ".github")
        fixture = repo / "tools/gba_wifi_link_release/fixtures"
        fixture.mkdir(parents=True)
        shutil.copy2(HISTORY, fixture / HISTORY.name)
        return temporary

    def assert_rejected_after(self, mutate) -> None:
        with self.copy_policy_repo() as temporary:
            repo = Path(temporary)
            workflow = repo / ".github/workflows/gba-wifi-link-ci.yml"
            workflow.write_text(mutate(workflow.read_text(encoding="utf-8")), encoding="utf-8")
            with self.assertRaises(WorkflowPolicyError):
                validate_workflow_policy(repo)

    def assert_release_rejected_after(self, mutate) -> None:
        with self.copy_policy_repo() as temporary:
            repo = Path(temporary)
            contract_source = ROOT / "packaging/gba-wifi-link/release/contract-v1.json"
            contract = repo / "packaging/gba-wifi-link/release/contract-v1.json"
            contract.parent.mkdir(parents=True)
            shutil.copy2(contract_source, contract)
            workflow = repo / ".github/workflows/gba-wifi-link-release.yml"
            workflow.write_text(mutate(workflow.read_text(encoding="utf-8")), encoding="utf-8")
            with self.assertRaises(WorkflowPolicyError):
                validate_release_workflow_policy(repo)

    def test_protected_workflow_satisfies_the_reusable_read_only_contract(self):
        validate_workflow_policy(ROOT)

    def test_fixture_job_requires_release_tooling_and_two_clean_package_builds(self):
        mutations = (
            lambda source: source.replace(
                "python3 -m unittest discover -s tools/gba_wifi_link_release/tests -p 'test_*.py' -v",
                "python3 -m unittest discover -s missing-release-tests -p 'test_*.py' -v",
                1,
            ),
            lambda source: source.replace(
                "diff --recursive --no-dereference \"$release_check_dir/first\" \\\n"
                "            \"$release_check_dir/second\"",
                "true # skip clean-directory release comparison",
                1,
            ),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_rejected_after(mutate)

    def test_yaml_lexer_skips_block_scalars_and_preserves_github_expressions(self):
        source = """name: ${{ github.workflow }}
jobs:
  example:
    steps:
      - name: ${{ github.sha }}
        run: |
          printf 'permissions: {id-token: write}\\n'
          printf '<<: *alias\\n'
"""
        self.assertEqual(
            lex_workflow_yaml(source),
            ("M:0:name", "M:0:jobs", "M:2:example", "M:4:steps", "M:6:-name", "M:8:run"),
        )

    def test_history_freezes_the_complete_normalized_ci_baseline(self):
        history = json.loads(HISTORY.read_text(encoding="utf-8"))
        baseline = history["ci_baseline"]
        self.assertEqual(
            baseline["gate_names"],
            [
                "Complete normal mGBA suite",
                "Focused tests (normal)",
                "Focused tests (ASan + UBSan)",
                "Focused tests (TSan)",
                "Fixture reproducibility",
                "Android arm64 libretro build",
            ],
        )
        self.assertEqual(baseline["job_ids"], [
            "full-normal-suite", "focused-tests", "fixture", "android-arm64",
        ])
        self.assertEqual(
            history["action_pins"],
            {
                "actions/checkout": {
                    "sha": "d23441a48e516b6c34aea4fa41551a30e30af803", "version": "v6",
                },
                "actions/download-artifact": {
                    "sha": "634f93cb2916e3fdff6788551b99b062d0335ce0", "version": "v5",
                },
                "actions/upload-artifact": {
                    "sha": "ea165f8d65b6e75b540449e92b4886f43607fa02", "version": "v4",
                },
            },
        )
        self.assertEqual(normalize_workflow(ROOT), baseline)

    def test_rejects_a_mutable_artifact_action_reference(self):
        self.assert_rejected_after(
            lambda source: source.replace(
                    "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02",
                    "actions/upload-artifact@v4",
            )
        )

    def test_rejects_mutable_source_commit_and_missing_checkout_identity_proofs(self):
        source_expression = "${{ inputs.source_commit || github.sha }}"
        proof = (
            "\n      - name: Verify checked out source commit\n"
            "        env:\n"
            f"          EXPECTED_SOURCE_COMMIT: {source_expression}\n"
            "        run: |\n"
            "          [[ \"$EXPECTED_SOURCE_COMMIT\" =~ ^[0-9a-f]{40}$ ]]\n"
            "          test \"$(git rev-parse HEAD)\" = \"$EXPECTED_SOURCE_COMMIT\"\n"
        )
        mutations = (
            lambda source: source.replace("ref: ${{ inputs.source_commit || github.sha }}", "ref: ${{ github.sha }}", 1),
            lambda source: source.replace("ref: ${{ inputs.source_commit || github.sha }}", "ref: ${{ github.sha }}"),
            lambda source: source.replace("        type: string", "        type: string\n        default: master", 1),
            lambda source: source.replace(proof, "", 1),
            lambda source: source.replace(
                '"source_commit": source_commit',
                '"source_commit": os.environ["EXPECTED_SOURCE_COMMIT"]',
                1,
            ),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_rejected_after(mutate)

    def test_requires_precheckout_source_commit_validation(self):
        validation = (
            "      - name: Validate exact source commit\n"
            "        env:\n"
            "          EXPECTED_SOURCE_COMMIT: ${{ inputs.source_commit || github.sha }}\n"
            "        run: |\n"
            "          [[ \"$EXPECTED_SOURCE_COMMIT\" =~ ^[0-9a-f]{40}$ ]]\n\n"
        )
        self.assert_rejected_after(lambda source: source.replace(validation, "", 1))

    def test_rejects_top_level_and_job_level_permission_writes(self):
        for mutate in (
            lambda source: source.replace("permissions:\n  contents: read", "permissions:\n  contents: read\n  id-token: write", 1),
            lambda source: source.replace(
                "  full-normal-suite:\n    name:",
                "  full-normal-suite:\n    permissions:\n      contents: read\n      id-token: write\n    name:",
                1,
            ),
        ):
            with self.subTest(mutation=mutate):
                self.assert_rejected_after(mutate)

    def test_rejects_every_noncanonical_permissions_key_spelling(self):
        job = "  full-normal-suite:\n    name: Complete normal mGBA suite\n"
        mutations = (
            lambda source: source.replace(
                job, job + "    permissions :\n      contents: read\n      id-token: write\n", 1,
            ),
            lambda source: source.replace(
                job, job + "    'permissions':\n      contents: read\n      id-token: write\n", 1,
            ),
            lambda source: source.replace(
                job, job + "\tpermissions:\n\t  contents: read\n\t  id-token: write\n", 1,
            ),
            lambda source: source.replace(
                job, job + "    \"permissions\" : {contents: read, id-token: write}\n", 1,
            ),
            lambda source: source.replace(
                job, job + "    ignored: { permissions: {id-token: write} }\n", 1,
            ),
            lambda source: source.replace(
                job, job + "    ? permissions\n    : {contents: read, id-token: write}\n", 1,
            ),
            lambda source: source.replace(
                job, job + '    "permiss\\u0069ons": {contents: read, id-token: write}\n', 1,
            ),
            lambda source: source.replace(
                job, job + "    !!str permissions: {contents: read, id-token: write}\n", 1,
            ),
            lambda source: source.replace(
                job,
                job + "    defaults: &write_permissions {id-token: write}\n    <<: *write_permissions\n",
                1,
            ),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_rejected_after(mutate)

    def test_rejects_drift_in_the_extracted_pinned_upstream_exception(self):
        pinned_step = (
            "\n      - name: Confirm pinned upstream util-hash baseline\n"
            "        shell: bash\n"
            "        run: |\n"
            "          set +e\n"
            "          output=\"$(ctest --test-dir build-ci-complete \\\n"
            "            --output-on-failure --tests-regex '^util-hash$' 2>&1)\"\n"
            "          status=$?\n"
            "          set -e\n"
            "          printf '%s\\n' \"$output\"\n"
            "          test \"$status\" -ne 0\n"
            "          grep --fixed-strings 'stagedCrc32' <<<\"$output\"\n"
            "          grep --fixed-strings '0% tests passed, 1 tests failed out of 1' \\\n"
            "            <<<\"$output\"\n"
        )
        mutations = (
            lambda source: source.replace("--exclude-regex '^util-hash$'", "--exclude-regex '^other$'", 1),
            lambda source: source.replace(pinned_step, "", 1),
            lambda source: source.replace("output=\"$(ctest --test-dir build-ci-complete", "output=\"$(true --test-dir build-ci-complete", 1),
            lambda source: source.replace("'stagedCrc32'", "'other failure'", 1),
            lambda source: source.replace('test "$status" -ne 0', 'test "$status" -eq 0', 1),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_rejected_after(mutate)

    def test_release_workflow_satisfies_the_automatic_tag_contract(self):
        validate_release_workflow_policy(ROOT)

    def test_canonical_publisher_handoff_contains_every_imported_module(self):
        source = (ROOT / ".github/workflows/gba-wifi-link-release.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn(" publisher render resource_limits text_policy verifier; do", source)

    def test_release_workflow_uses_only_trusted_tag_publication_permissions(self):
        source = (ROOT / ".github/workflows/gba-wifi-link-release.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("  push:\n    tags:\n      - v*", source)
        self.assertNotIn("workflow_dispatch", source)
        self.assertIn("  publish:\n    name: Automatically publish", source)
        self.assertIn("    permissions:\n      contents: write\n", source)
        self.assertNotIn("attestations:", source)
        self.assertNotIn("id-token:", source)
        self.assertNotIn("actions/attest-build-provenance", source)

    def test_release_workflow_has_two_post_validation_clean_compile_jobs(self):
        source = (ROOT / ".github/workflows/gba-wifi-link-release.yml").read_text(encoding="utf-8")
        self.assertEqual(
            source.count("cmake --build build-release-android --parallel 2 --target mgba_libretro"),
            2,
        )
        self.assertEqual(source.count("-DSKIP_GIT=ON"), 2)
        self.assertEqual(source.count('-DGIT_COMMIT="$EXPECTED_COMMIT"'), 2)
        self.assertEqual(source.count('-DGIT_TAG="$EXPECTED_TAG"'), 2)
        self.assertEqual(
            source.count('SOURCE_DATE_EPOCH="$(git show -s --format=%ct "$EXPECTED_COMMIT")"'),
            2,
        )

    def test_release_workflow_rejects_missing_or_mismatched_actual_build_evidence(self):
        mutations = (
            lambda source: source.replace(
                '              "runner_image_os": os.environ["ImageOS"],\n', "", 1
            ),
            lambda source: source.replace(
                '              "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),\n',
                "", 1,
            ),
            lambda source: source.replace(
                '              "pinned_actions": pinned_actions,\n', "", 1
            ),
            lambda source: source.replace(
                '              "job_id": job["id"],\n', "", 1
            ),
            lambda source: source.replace(
                '              "compiler_sha256",\n              "compiler_version",\n',
                '              "compiler_version",\n',
                1,
            ),
            lambda source: source.replace(
                "python3 tools/gba-wifi-link-release.py bind-builds",
                "cp admitted/release-context.json context.json # missing actual identities",
                1,
            ),
            lambda source: source.replace(
                'f"{name}@{workflow[\'action_versions\'][name]}+sha:',
                'f"{name}@sha:',
                1,
            ),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_release_rejected_after(mutate)

    def test_release_workflow_exact_rerun_is_read_only_through_publisher(self):
        source = (ROOT / ".github/workflows/gba-wifi-link-release.yml").read_text(encoding="utf-8")
        self.assertIn("tools/gba-wifi-link-release.py publish", source)
        self.assertNotIn("release_exists: ${{ steps.evidence.outputs.release_exists }}", source)

    def test_release_workflow_rejects_trigger_concurrency_or_human_gate_mutations(self):
        mutations = (
            lambda source: source.replace("      - v*", "      - release-*", 1),
            lambda source: source.replace("on:\n  push:", "on:\n  workflow_dispatch:\n  push:", 1),
            lambda source: source.replace("github.ref }}", "github.sha }}", 1),
            lambda source: source.replace("cancel-in-progress: false", "cancel-in-progress: true", 1),
            lambda source: source.replace(
                "  publish:\n    name: Automatically publish",
                "  publish:\n    environment: production\n    name: Automatically publish",
                1,
            ),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_release_rejected_after(mutate)

    def test_release_workflow_rejects_permission_boundary_mutations(self):
        mutations = (
            lambda source: source.replace(
                "permissions:\n  actions: read\n  contents: read",
                "permissions:\n  actions: read\n  contents: write",
                1,
            ),
            lambda source: source.replace("      contents: write", "      contents: read", 1),
            lambda source: source.replace(
                "  compare-builds:\n    name:",
                "  compare-builds:\n    permissions:\n      contents: write\n    name:",
                1,
            ),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_release_rejected_after(mutate)

    def test_release_workflow_rejects_graph_and_reproducibility_mutations(self):
        mutations = (
            lambda source: source.replace(
                "    needs: [inspect-tag, protected-validation]\n",
                "    needs: inspect-tag\n",
                1,
            ),
            lambda source: source.replace(
                "    needs: [inspect-tag, admit]\n",
                "    needs: inspect-tag\n",
                1,
            ),
            lambda source: source.replace(
                "    needs: [inspect-tag, admit, protected-build, independent-build]",
                "    needs: [inspect-tag, admit, protected-build]",
                1,
            ),
            lambda source: source.replace(
                "cmp --silent build-protected/mgba_libretro_android.so build-independent/mgba_libretro_android.so",
                "true",
                1,
            ),
            lambda source: source.replace("sha256sum --check build-digests.txt", "true", 1),
            lambda source: source.replace(
                "    needs: [inspect-tag, compare-builds]\n",
                "    needs: inspect-tag\n",
                1,
            ),
            lambda source: source.replace(
                "    needs: [admit, package]\n", "    needs: [admit, compare-builds]\n", 1
            ),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_release_rejected_after(mutate)

    def test_release_workflow_rejects_action_pin_and_artifact_handoff_mutations(self):
        mutations = (
            lambda source: source.replace("actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803", "actions/checkout@v6", 1),
            lambda source: source.replace("actions/download-artifact@634f93cb2916e3fdff6788551b99b062d0335ce0", "actions/download-artifact@v5", 1),
            lambda source: source.replace("actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02", "actions/upload-artifact@v4", 1),
            lambda source: source.replace("name: gba-wifi-link-release-canonical", "name: arbitrary-publisher-input", 1),
            lambda source: source.replace("if-no-files-found: error", "if-no-files-found: warn", 1),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_release_rejected_after(mutate)

    def test_release_workflow_rejects_publisher_isolation_or_ordering_mutations(self):
        mutations = (
            lambda source: source.replace(
                "    steps:\n      - name: Download canonical publisher input",
                "    steps:\n      - name: Check out source\n        uses: actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803 # v6\n\n      - name: Download canonical publisher input",
                1,
            ),
            lambda source: source.replace(
                "      - name: Verify canonical release before mutation",
                "      - name: Build replacement core\n        run: cmake --build build\n\n      - name: Verify canonical release before mutation",
                1,
            ),
            lambda source: source.replace(
                "          set -euo pipefail\n          test \"$(find publisher-input",
                "          set -euo pipefail\n          gh api --method POST repos/$GITHUB_REPOSITORY/releases\n          test \"$(find publisher-input",
                1,
            ),
            lambda source: source.replace(
                "      - name: Recheck immutable remote tag",
                "      - name: Mutate release before tag recheck\n"
                "        run: gh api --method PATCH repos/$GITHUB_REPOSITORY/releases/1\n\n"
                "      - name: Recheck immutable remote tag",
                1,
            ),
            lambda source: source.replace(
                "      - name: Publish transaction and final public verification",
                "      - name: Undeclared release command\n"
                "        run: gh release create v0.0.0\n\n"
                "      - name: Publish transaction and final public verification",
                1,
            ),
            lambda source: source.replace(" verify-tag ", " render-body ", 1),
            lambda source: source.replace(
                "      - name: Publish transaction and final public verification",
                "      - name: Publish transaction and final public verification\n        if: always()",
                1,
            ),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_release_rejected_after(mutate)

    def test_release_workflow_rejects_post_verification_package_mutations(self):
        mutations = (
            lambda source: source.replace(
                "          cmp --silent release-body-a.md release-body-b.md\n",
                "          cmp --silent release-body-a.md release-body-b.md\n"
                "          printf 'unreviewed body\\n' > release-body-a.md\n",
                1,
            ),
            lambda source: source.replace(
                "          python3 tools/gba-wifi-link-release.py verify --context context.json --output package-b/release\n",
                "          python3 tools/gba-wifi-link-release.py verify --context context.json --output package-b/release\n"
                "          printf 'corruption\\n' >> package-a/release/INSTALL-AND-USAGE.md\n",
                1,
            ),
            lambda source: source.replace(
                "          cp release-body-a.md publisher-input/release-body.md\n",
                "          cp release-body-a.md publisher-input/release-body.md\n"
                "          printf 'unreviewed body\\n' > publisher-input/release-body.md\n",
                1,
            ),
            lambda source: source.replace(
                "            xargs -0 sha256sum > publisher-input/release-tool/MANIFEST.sha256\n",
                "            xargs -0 sha256sum > publisher-input/release-tool/MANIFEST.sha256\n"
                "          printf 'forged manifest\\n' >> publisher-input/release-tool/MANIFEST.sha256\n",
                1,
            ),
            lambda source: source.replace(
                "      - name: Upload one immutable canonical publisher artifact\n",
                "      - name: Substitute verified handoff\n"
                "        run: cp release-body-b.md publisher-input/release-body.md\n\n"
                "      - name: Upload one immutable canonical publisher artifact\n",
                1,
            ),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_release_rejected_after(mutate)

    def test_release_workflow_rejects_missing_gate_corruption_or_tag_guards(self):
        mutations = (
            lambda source: source.replace("        required = (", "        required = (\n            # missing gate", 1).replace(
                '            "Fixture reproducibility",\n', "", 1
            ),
            lambda source: source.replace("verify --context context.json --output package-b/release", "true # missing second package verification", 1),
            lambda source: source.replace(" verify-tag ", " verify ", 1),
        )
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                self.assert_release_rejected_after(mutate)
