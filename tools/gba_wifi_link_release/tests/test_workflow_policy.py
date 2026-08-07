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
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            shutil.copytree(ROOT / ".github", repo / ".github")
            fixture = repo / "tools/gba_wifi_link_release/fixtures"
            fixture.mkdir(parents=True)
            shutil.copy2(HISTORY, fixture / HISTORY.name)
            workflow = repo / ".github/workflows/gba-wifi-link-ci.yml"
            workflow.write_text(
                workflow.read_text(encoding="utf-8").replace(
                    "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02",
                    "actions/upload-artifact@v4",
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(WorkflowPolicyError, "^WORKFLOW_ACTION_PIN$"):
                validate_workflow_policy(repo)
