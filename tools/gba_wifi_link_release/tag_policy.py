"""Fail-closed validation for the immutable GBA Wi-Fi Link release-tag ruleset."""

import copy
import json
import os
from pathlib import Path
import subprocess


_EXPECTED_POLICY_KEYS = frozenset({"schema", "repository", "ruleset"})
_EXPECTED_RULESET_KEYS = frozenset({
    "bypass_actors", "conditions", "enforcement", "name", "rules", "target",
})
_GITHUB_GENERATED_KEYS = frozenset({
    "_links", "created_at", "current_user_can_bypass", "id", "node_id",
    "source", "source_type", "updated_at",
})
_MAX_POLICY_BYTES = 1 << 20
_MAX_DIFFERENCE_POINTER = 160
_MAX_AUDIT_TOKEN_BYTES = 4096
RULESET_AUDIT_TOKEN_ENV = "GBA_WIFI_LINK_RULESET_AUDIT_TOKEN"
_CANONICAL_REPOSITORY = "Aelvryx/mgba-wifi-link"


class TagPolicyError(ValueError):
    """A stable failure category for tag-policy verification."""


def _duplicate_free_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise TagPolicyError("TAG_POLICY_JSON")
        result[key] = value
    return result


def _pointer_escape(value: str) -> str:
    return value.replace("~", "~0").replace("/", "~1")


def _first_difference(expected: object, actual: object, pointer: str = "") -> str | None:
    if type(expected) is not type(actual):
        return pointer or "/"
    if isinstance(expected, dict):
        expected_keys = set(expected)
        actual_keys = set(actual)
        for key in sorted(expected_keys | actual_keys):
            child = f"{pointer}/{_pointer_escape(key)}"
            if key not in expected or key not in actual:
                return child
            result = _first_difference(expected[key], actual[key], child)
            if result is not None:
                return result
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{pointer}/length"
        for index, (expected_child, actual_child) in enumerate(zip(expected, actual)):
            result = _first_difference(expected_child, actual_child, f"{pointer}/{index}")
            if result is not None:
                return result
        return None
    return None if expected == actual else (pointer or "/")


def _require_policy_shape(policy: object) -> dict[str, object]:
    if not isinstance(policy, dict) or set(policy) != _EXPECTED_POLICY_KEYS:
        raise TagPolicyError("TAG_POLICY_SCHEMA")
    if policy.get("schema") != 1 or policy.get("repository") != _CANONICAL_REPOSITORY:
        raise TagPolicyError("TAG_POLICY_SCHEMA")
    ruleset = policy.get("ruleset")
    if not isinstance(ruleset, dict) or set(ruleset) != _EXPECTED_RULESET_KEYS:
        raise TagPolicyError("TAG_POLICY_SCHEMA")
    return policy


def load_tag_policy(path: Path) -> dict[str, object]:
    """Read the exact tracked policy without accepting duplicate JSON keys."""
    try:
        source = path.read_bytes()
        if len(source) > _MAX_POLICY_BYTES:
            raise TagPolicyError("TAG_POLICY_INPUT")
        policy = json.loads(source.decode("utf-8"), object_pairs_hook=_duplicate_free_object,
                            parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
    except (OSError, UnicodeDecodeError, ValueError, json.JSONDecodeError) as error:
        if isinstance(error, TagPolicyError):
            raise
        raise TagPolicyError("TAG_POLICY_INPUT") from error
    return _require_policy_shape(policy)


def _load_json_bytes(source: bytes) -> object:
    if len(source) > _MAX_POLICY_BYTES:
        raise TagPolicyError("TAG_POLICY_RESPONSE")
    try:
        return json.loads(source.decode("utf-8"), object_pairs_hook=_duplicate_free_object,
                          parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
    except (UnicodeDecodeError, ValueError, json.JSONDecodeError) as error:
        if isinstance(error, TagPolicyError):
            raise
        raise TagPolicyError("TAG_POLICY_RESPONSE") from error


def canonicalize_tag_ruleset(actual: object) -> dict[str, object]:
    """Remove only GitHub-generated response metadata from one selected ruleset."""
    if not isinstance(actual, dict):
        raise TagPolicyError("TAG_POLICY_RULESET")
    result = copy.deepcopy(actual)
    unexpected = set(result) - _EXPECTED_RULESET_KEYS - _GITHUB_GENERATED_KEYS
    if unexpected:
        raise TagPolicyError("TAG_POLICY_DRIFT /" + _pointer_escape(sorted(unexpected)[0]))
    if "source" in result and result["source"] != _CANONICAL_REPOSITORY:
        raise TagPolicyError("TAG_POLICY_DRIFT /source")
    if "source_type" in result and result["source_type"] != "Repository":
        raise TagPolicyError("TAG_POLICY_DRIFT /source_type")
    for key in _GITHUB_GENERATED_KEYS:
        result.pop(key, None)
    if set(result) != _EXPECTED_RULESET_KEYS:
        raise TagPolicyError("TAG_POLICY_RULESET")
    return result


def validate_tag_ruleset(actual: object, expected: object) -> None:
    """Compare every semantic ruleset field, reporting one bounded pointer only."""
    policy = _require_policy_shape(expected)
    canonical = canonicalize_tag_ruleset(actual)
    difference = _first_difference(policy["ruleset"], canonical)
    if difference is not None:
        raise TagPolicyError("TAG_POLICY_DRIFT " + difference[:_MAX_DIFFERENCE_POINTER])


def select_tag_ruleset_id(rulesets: object, expected: object) -> int:
    """Select one ruleset ID from GitHub's intentionally summary-only list."""
    policy = _require_policy_shape(expected)
    if not isinstance(rulesets, list):
        raise TagPolicyError("TAG_POLICY_RESPONSE")
    wanted = policy["ruleset"]
    assert isinstance(wanted, dict)
    named = [ruleset for ruleset in rulesets
             if isinstance(ruleset, dict) and ruleset.get("name") == wanted["name"]]
    if any(
        ruleset.get("source") != _CANONICAL_REPOSITORY
        or ruleset.get("source_type") != "Repository"
        or type(ruleset.get("id")) is not int
        or ruleset["id"] <= 0
        for ruleset in named
    ):
        raise TagPolicyError("TAG_POLICY_RULESET")
    matches = named
    if len(matches) != 1:
        raise TagPolicyError("TAG_POLICY_RULESET")
    return matches[0]["id"]


def validate_tag_ruleset_detail(actual: object, expected: object, ruleset_id: int) -> None:
    """Validate the complete selected response, including hidden-sensitive fields."""
    if (
        type(ruleset_id) is not int
        or ruleset_id <= 0
        or not isinstance(actual, dict)
        or actual.get("id") != ruleset_id
        or actual.get("source") != _CANONICAL_REPOSITORY
        or actual.get("source_type") != "Repository"
        or "bypass_actors" not in actual
    ):
        raise TagPolicyError("TAG_POLICY_DETAIL")
    validate_tag_ruleset(actual, expected)


def _require_audit_token(audit_token: object) -> str:
    if (
        not isinstance(audit_token, str)
        or not audit_token
        or len(audit_token.encode("utf-8")) > _MAX_AUDIT_TOKEN_BYTES
        or any(character.isspace() for character in audit_token)
    ):
        raise TagPolicyError("TAG_POLICY_AUDIT_CREDENTIAL")
    return audit_token


def _read_api(endpoint: str, audit_token: object, *, gh: str) -> object:
    token = _require_audit_token(audit_token)
    if not isinstance(gh, str) or not gh:
        raise TagPolicyError("TAG_POLICY_GITHUB")
    environment = os.environ.copy()
    environment.pop("GITHUB_TOKEN", None)
    environment.pop("GH_TOKEN", None)
    environment["GH_TOKEN"] = token
    try:
        completed = subprocess.run(
            [gh, "api", "--method", "GET", endpoint], check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise TagPolicyError("TAG_POLICY_GITHUB") from error
    return _load_json_bytes(completed.stdout)


def read_tag_rulesets(repository: str, audit_token: object, *, gh: str = "gh") -> object:
    """Read GitHub's ruleset summaries with the explicitly provisioned auditor."""
    if repository != _CANONICAL_REPOSITORY:
        raise TagPolicyError("TAG_POLICY_REPOSITORY")
    return _read_api(f"repos/{repository}/rulesets?per_page=100", audit_token, gh=gh)


def read_tag_ruleset_detail(repository: str, ruleset_id: int, audit_token: object,
                             *, gh: str = "gh") -> object:
    """Read one complete ruleset response after a summary-only list selection."""
    if repository != _CANONICAL_REPOSITORY or type(ruleset_id) is not int or ruleset_id <= 0:
        raise TagPolicyError("TAG_POLICY_REPOSITORY")
    return _read_api(f"repos/{repository}/rulesets/{ruleset_id}", audit_token, gh=gh)


def verify_live_tag_policy(repository: str, expected: object, *, audit_token: object,
                           gh: str = "gh") -> None:
    """Perform the two-request, GET-only live ruleset verification contract."""
    ruleset_id = select_tag_ruleset_id(read_tag_rulesets(repository, audit_token, gh=gh), expected)
    detail = read_tag_ruleset_detail(repository, ruleset_id, audit_token, gh=gh)
    validate_tag_ruleset_detail(detail, expected, ruleset_id)
