"""Fail-closed validation for the immutable GBA Wi-Fi Link release-tag ruleset."""

import copy
import json
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
    if policy.get("schema") != 1 or policy.get("repository") != "Aelvryx/mgba-wifi-link":
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
    if "source" in result and result["source"] != "Aelvryx/mgba-wifi-link":
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


def select_tag_ruleset(rulesets: object, expected: object) -> dict[str, object]:
    """Select exactly the expected named tag ruleset from the repository response."""
    policy = _require_policy_shape(expected)
    if not isinstance(rulesets, list):
        raise TagPolicyError("TAG_POLICY_RESPONSE")
    wanted = policy["ruleset"]
    assert isinstance(wanted, dict)
    matches = [
        ruleset for ruleset in rulesets
        if isinstance(ruleset, dict)
        and ruleset.get("name") == wanted["name"]
        and ruleset.get("target") == wanted["target"]
    ]
    if len(matches) != 1:
        raise TagPolicyError("TAG_POLICY_RULESET")
    return matches[0]


def validate_tag_policy_response(rulesets: object, expected: object) -> None:
    """Select and validate the one live immutable release-tag ruleset."""
    validate_tag_ruleset(select_tag_ruleset(rulesets, expected), expected)


def read_tag_rulesets(repository: str, *, gh: str = "gh") -> object:
    """Read the sole canonical repository's rulesets without mutating GitHub."""
    if repository != "Aelvryx/mgba-wifi-link" or not isinstance(gh, str) or not gh:
        raise TagPolicyError("TAG_POLICY_REPOSITORY")
    try:
        completed = subprocess.run(
            [gh, "api", f"repos/{repository}/rulesets?per_page=100"], check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise TagPolicyError("TAG_POLICY_GITHUB") from error
    return _load_json_bytes(completed.stdout)
