"""Static contracts for the protected GBA Wi-Fi Link validation workflow."""

import json
from pathlib import Path
import shutil
import tempfile
import unittest

from tools.gba_wifi_link_release.workflow_policy import (
    WorkflowPolicyError,
    normalize_workflow,
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

    def test_protected_workflow_satisfies_the_reusable_read_only_contract(self):
        validate_workflow_policy(ROOT)

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
                "actions/attest-build-provenance": {
                    "sha": "977bb373ede98d70efdf65b84cb5f73e068dcc2a", "version": "v3",
                },
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
