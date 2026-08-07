"""Regression tests for the immutable GBA Wi-Fi Link release-tag policy."""

import copy
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import json
import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

from tools.gba_wifi_link_release.cli import main
from tools.gba_wifi_link_release.tag_policy import (
    TagPolicyError,
    load_tag_policy,
    select_tag_ruleset_id,
    validate_tag_ruleset,
    verify_live_tag_policy,
)


ROOT = Path(__file__).resolve().parents[3]
POLICY = ROOT / ".github/rulesets/gba-wifi-link-release-tags.json"
RULESET_FIXTURES = ROOT / "tools/gba_wifi_link_release/fixtures/rulesets"


class TagPolicyTest(unittest.TestCase):
    def setUp(self) -> None:
        self.expected = load_tag_policy(POLICY)
        self.actual = copy.deepcopy(self.expected["ruleset"])

    def assert_rejected(self, mutate) -> None:
        actual = copy.deepcopy(self.actual)
        mutate(actual)
        with self.assertRaises(TagPolicyError):
            validate_tag_ruleset(actual, self.expected)

    def summary(self, ruleset_id: int = 123) -> dict[str, object]:
        value = json.loads((RULESET_FIXTURES / "list-summary.json").read_text(encoding="utf-8"))[0]
        value["id"] = ruleset_id
        value["_links"]["self"]["href"] = value["_links"]["self"]["href"].rsplit("/", 1)[0] + f"/{ruleset_id}"
        return value

    def detail(self, ruleset_id: int = 123) -> dict[str, object]:
        value = json.loads((RULESET_FIXTURES / "detail.json").read_text(encoding="utf-8"))
        value["id"] = ruleset_id
        value["_links"]["self"]["href"] = value["_links"]["self"]["href"].rsplit("/", 1)[0] + f"/{ruleset_id}"
        return value

    def test_tracked_policy_defines_the_exact_canonical_immutable_tag_contract(self):
        validate_tag_ruleset(self.actual, self.expected)
        self.assertEqual(self.expected["repository"], "Aelvryx/mgba-wifi-link")
        self.assertEqual(self.actual["name"], "GBA Wi-Fi Link release tags")
        self.assertEqual(self.actual["target"], "tag")
        self.assertEqual(self.actual["enforcement"], "active")
        self.assertEqual(self.actual["conditions"]["ref_name"], {
            "include": ["refs/tags/v*"], "exclude": [],
        })
        self.assertEqual(self.actual["bypass_actors"], [])
        self.assertEqual(
            self.actual["rules"],
            [{"type": "deletion"}, {"type": "non_fast_forward"}],
        )

    def test_rejects_noncanonical_repository_target_enforcement_or_pattern(self):
        for mutate in (
            lambda actual: actual.__setitem__("target", "branch"),
            lambda actual: actual.__setitem__("enforcement", "evaluate"),
            lambda actual: actual["conditions"]["ref_name"].__setitem__(
                "include", ["refs/tags/*"]
            ),
            lambda actual: actual["conditions"]["ref_name"].__setitem__(
                "exclude", ["refs/tags/v0.*"]
            ),
        ):
            with self.subTest(mutation=mutate):
                self.assert_rejected(mutate)

    def test_rejects_creation_restriction_or_missing_update_and_deletion_protection(self):
        for mutate in (
            lambda actual: actual.__setitem__("rules", [{"type": "deletion"}]),
            lambda actual: actual.__setitem__("rules", [{"type": "non_fast_forward"}]),
            lambda actual: actual.__setitem__(
                "rules", [{"type": "creation"}, {"type": "deletion"}, {"type": "non_fast_forward"}]
            ),
            lambda actual: actual["rules"].append({"type": "required_signatures"}),
        ):
            with self.subTest(mutation=mutate):
                self.assert_rejected(mutate)

    def test_rejects_bypass_actors_and_unexpected_semantic_drift(self):
        for mutate in (
            lambda actual: actual.__setitem__(
                "bypass_actors", [{"actor_id": 1, "actor_type": "RepositoryRole", "bypass_mode": "always"}]
            ),
            lambda actual: actual.__setitem__("name", "similar but wrong"),
            lambda actual: actual.__setitem__("unknown", True),
            lambda actual: actual.__setitem__("source", "example/other"),
        ):
            with self.subTest(mutation=mutate):
                self.assert_rejected(mutate)

    def test_canonicalizes_only_github_generated_metadata(self):
        actual = copy.deepcopy(self.actual)
        actual.update({
            "id": 123,
            "node_id": "RRS_kwDOExample",
            "created_at": "2026-08-07T00:00:00Z",
            "updated_at": "2026-08-07T00:00:00Z",
            "source": "Aelvryx/mgba-wifi-link",
            "source_type": "Repository",
            "_links": {"self": {"href": "https://api.github.com/repos/Aelvryx/mgba-wifi-link/rulesets/123"}},
        })
        validate_tag_ruleset(actual, self.expected)

    def test_selects_one_exact_name_and_canonical_source_from_a_real_list_summary(self):
        result = select_tag_ruleset_id(
            [
                {"name": "unrelated", "target": "branch"},
                self.summary(),
            ],
            self.expected,
        )
        self.assertEqual(result, 123)
        with self.assertRaisesRegex(TagPolicyError, "TAG_POLICY_RULESET"):
            select_tag_ruleset_id([self.summary(), self.summary(456)], self.expected)
        with self.assertRaisesRegex(TagPolicyError, "TAG_POLICY_RULESET"):
            select_tag_ruleset_id([], self.expected)
        with self.assertRaisesRegex(TagPolicyError, "TAG_POLICY_RULESET"):
            select_tag_ruleset_id([{**self.summary(), "id": "123"}], self.expected)
        with self.assertRaisesRegex(TagPolicyError, "TAG_POLICY_RULESET"):
            select_tag_ruleset_id([{**self.summary(), "source": "example/other"}], self.expected)

    def test_policy_file_is_strict_json(self):
        with self.assertRaises(TagPolicyError):
            load_tag_policy(POLICY.with_name("missing.json"))
        self.assertEqual(json.loads(POLICY.read_text(encoding="utf-8"))["schema"], 1)

    def test_cli_reads_and_validates_only_the_canonical_repository_rulesets(self):
        stdout = StringIO()
        stderr = StringIO()
        response = {
            "repos/Aelvryx/mgba-wifi-link/rulesets?per_page=100": json.dumps([self.summary()]).encode("utf-8"),
            "repos/Aelvryx/mgba-wifi-link/rulesets/123": json.dumps(self.detail()).encode("utf-8"),
        }
        with patch("tools.gba_wifi_link_release.tag_policy.subprocess.run") as run:
            run.side_effect = lambda arguments, **kwargs: subprocess.CompletedProcess(
                (), 0, stdout=response[arguments[-1]]
            )
            with patch.dict(os.environ, {"GBA_WIFI_LINK_RULESET_AUDIT_TOKEN": "test-token"}, clear=True):
                with redirect_stdout(stdout), redirect_stderr(stderr):
                    self.assertEqual(main(["verify-tag-policy"]), 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(stdout.getvalue(), "tag policy verified\n")
        self.assertEqual(
            run.call_args_list[0].args[0],
            ["gh", "api", "--method", "GET", "repos/Aelvryx/mgba-wifi-link/rulesets?per_page=100"],
        )
        self.assertEqual(
            run.call_args_list[1].args[0],
            ["gh", "api", "--method", "GET", "repos/Aelvryx/mgba-wifi-link/rulesets/123"],
        )
        self.assertEqual(run.call_args_list[0].kwargs["env"]["GH_TOKEN"], "test-token")
        self.assertNotIn("GITHUB_TOKEN", run.call_args_list[0].kwargs["env"])

    def test_cli_rejects_a_missing_or_drifted_live_tag_ruleset(self):
        stderr = StringIO()
        with patch("tools.gba_wifi_link_release.tag_policy.subprocess.run") as run:
            run.return_value = subprocess.CompletedProcess((), 0, stdout=b"[]")
            with patch.dict(os.environ, {"GBA_WIFI_LINK_RULESET_AUDIT_TOKEN": "test-token"}, clear=True):
                with redirect_stderr(stderr):
                    self.assertEqual(main(["verify-tag-policy"]), 2)
        self.assertIn("TAG_POLICY_RULESET", stderr.getvalue())

    def test_rejects_detail_drift_or_withheld_bypass_actors_after_summary_selection(self):
        detail = self.detail()
        del detail["bypass_actors"]
        for selected_detail in (
            {**self.detail(), "enforcement": "evaluate"},
            detail,
        ):
            with self.subTest(detail=selected_detail):
                calls: list[list[str]] = []

                def run(arguments, **kwargs):
                    calls.append(arguments)
                    if arguments[-1].endswith("?per_page=100"):
                        return subprocess.CompletedProcess((), 0, stdout=json.dumps([self.summary()]).encode("utf-8"))
                    return subprocess.CompletedProcess((), 0, stdout=json.dumps(selected_detail).encode("utf-8"))

                with patch("tools.gba_wifi_link_release.tag_policy.subprocess.run", side_effect=run):
                    with self.assertRaises(TagPolicyError):
                        verify_live_tag_policy(
                            "Aelvryx/mgba-wifi-link", self.expected, audit_token="test-token"
                        )
                self.assertEqual(len(calls), 2)

    def test_cli_fails_closed_without_the_explicit_ruleset_audit_credential(self):
        stderr = StringIO()
        with patch.dict(os.environ, {}, clear=True):
            with redirect_stderr(stderr):
                self.assertEqual(main(["verify-tag-policy"]), 2)
        self.assertIn("TAG_POLICY_AUDIT_CREDENTIAL", stderr.getvalue())
