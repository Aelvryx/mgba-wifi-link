"""Fail-closed static policy checks for the protected CI workflow.

The project intentionally avoids a YAML parser here.  The workflow is a small,
reviewed source contract, so these checks validate its exact, security-relevant
spelling and normalize only the behavioral fields frozen in the history fixture.
"""

import json
from pathlib import Path
import re


_WORKFLOW = Path(".github/workflows/gba-wifi-link-ci.yml")
_HISTORY = Path("tools/gba_wifi_link_release/fixtures/v0.2.0-history.json")
_SHA = re.compile(r"^[0-9a-f]{40}$")
_USES = re.compile(r"^\s*uses:\s*([^\s#]+)(?:\s+#.*)?\s*$", re.MULTILINE)
_SOURCE_COMMIT = "${{ inputs.source_commit || github.sha }}"


class WorkflowPolicyError(ValueError):
    """A stable reason that a workflow source contract was violated."""


def _read(repo: Path) -> tuple[str, dict[str, object]]:
    try:
        text = (repo / _WORKFLOW).read_text(encoding="utf-8")
        history = json.loads((repo / _HISTORY).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise WorkflowPolicyError("WORKFLOW_INPUT") from error
    if not isinstance(history.get("ci_baseline"), dict):
        raise WorkflowPolicyError("WORKFLOW_BASELINE")
    return text, history


def _step(text: str, name: str) -> str:
    marker = f"      - name: {name}\n"
    start = text.find(marker)
    if start < 0:
        raise WorkflowPolicyError("WORKFLOW_BASELINE")
    end = text.find("\n      - name: ", start + len(marker))
    return text[start:] if end < 0 else text[start:end]


def _compact(value: str) -> str:
    return re.sub(r"\s+", " ", re.sub(r"\\\s*\n\s*", " ", value)).strip()


def _cmake_flags(block: str) -> list[str]:
    compact = _compact(block).split("cmake -S ", 1)[-1]
    return re.findall(r"-D[A-Z0-9_]+=(?:\"[^\"]*\"|[^ ]+)", compact)


def _required(text: str, fragment: str) -> None:
    if fragment not in text:
        raise WorkflowPolicyError("WORKFLOW_BASELINE")


def _upstream_exception(text: str) -> dict[str, object]:
    """Extract the exclusion and the separately asserted upstream failure."""
    excluded = _step(text, "Run all tests except pinned upstream failure")
    exclusion = re.search(r"--exclude-regex '([^']+)'", excluded)
    try:
        pinned = _step(text, "Confirm pinned upstream util-hash baseline")
    except WorkflowPolicyError as error:
        raise WorkflowPolicyError("WORKFLOW_UPSTREAM_EXCEPTION") from error
    compact = _compact(pinned)
    command = re.search(r'output="\$\(([^)]*)\)"', compact)
    marker = re.search(r"grep --fixed-strings '([^']+)' <<<\"\$output\"", compact)
    summary = re.findall(r"grep --fixed-strings '([^']+)'", compact)
    pinned_regex = (
        re.search(r"--tests-regex '([^']+)'", command.group(1))
        if command is not None else None
    )
    if (
        exclusion is None
        or command is None
        or marker is None
        or len(summary) != 2
        or pinned_regex is None
        or "shell: bash" not in pinned
        or "set +e" not in pinned
        or "status=$?" not in pinned
        or "set -e" not in pinned
        or 'test "$status" -ne 0' not in pinned
    ):
        raise WorkflowPolicyError("WORKFLOW_UPSTREAM_EXCEPTION")
    return {
        "excluded_test_regex": exclusion.group(1),
        "failure_marker": marker.group(1),
        "failure_summary": summary[1],
        "pinned_command": command.group(1),
        "pinned_test_regex": pinned_regex.group(1),
        "requires_nonzero_status": True,
        "shell": "bash",
    }


def normalize_workflow(repo: Path) -> dict[str, object]:
    """Extract every frozen CI behavior from the reviewed workflow source."""
    text, _ = _read(repo)
    if not re.search(r"^  push:\n    branches:\n      - master$", text, re.MULTILINE):
        raise WorkflowPolicyError("WORKFLOW_TRIGGER")
    if not re.search(r"^  pull_request:\s*$", text, re.MULTILINE):
        raise WorkflowPolicyError("WORKFLOW_TRIGGER")

    job_ids = re.findall(r"^  ([a-z0-9-]+):\n    name:", text, re.MULTILINE)
    expected_ids = ["full-normal-suite", "focused-tests", "fixture", "android-arm64"]
    if job_ids != expected_ids:
        raise WorkflowPolicyError("WORKFLOW_JOBS")
    job_names = {
        job_id: re.search(
            rf"^  {re.escape(job_id)}:\n    name: (.+)$", text, re.MULTILINE,
        ).group(1)
        for job_id in job_ids
    }

    complete = _step(text, "Configure complete suite")
    focused = _step(text, "Configure focused suite")
    fixture = _step(text, "Rebuild and compare test-ROM fixture")
    android = _step(text, "Configure Android arm64 core")
    target_block = _step(text, "Build focused suite")
    test_block = _step(text, "Run focused suite")
    fixture_area = text[text.index("  fixture:\n"):text.index("  android-arm64:\n")]
    android_area = text[text.index("  android-arm64:\n"):]

    target_section = target_block.split("--target", 1)[-1]
    focused_targets = re.findall(r"\b(test-[a-z0-9-]+)\b", target_section)
    regex_match = re.search(r"--tests-regex '([^']+)'", test_block)
    if regex_match is None:
        raise WorkflowPolicyError("WORKFLOW_BASELINE")
    sanitizer_matrix = [
        {"name": name, "build_dir": directory, "sanitizers": sanitizers}
        for name, directory, sanitizers in re.findall(
            r"      - name: (.+)\n            build_dir: (.+)\n            sanitizers: (.+)", text,
        )
    ]
    sanitizer_environment = dict(re.findall(
        r"^          (ASAN_OPTIONS|LSAN_OPTIONS|TSAN_OPTIONS|UBSAN_OPTIONS): (.+)$",
        test_block,
        re.MULTILINE,
    ))
    toolchain = dict(re.findall(
        r"^      (ARM_TOOLCHAIN_ARCHIVE|ARM_TOOLCHAIN_SHA256): (.+)$",
        fixture_area,
        re.MULTILINE,
    ))
    ndk = re.search(r"^      ANDROID_NDK_VERSION: (.+)$", android_area, re.MULTILINE)
    if ndk is None:
        raise WorkflowPolicyError("WORKFLOW_BASELINE")

    fixture_commands = [
        "make -C tools/gba-link-test-rom verify-fixture CROSS=/tmp/gba-link-arm-toolchain/bin/arm-none-eabi-",
        "python3 tools/test-analyze-gba-wifi-link.py",
        "bash -n tools/four-swords-discovery/android-qualification.sh",
        "python3 tools/four-swords-discovery/test-qualification-helper.py",
    ]
    boundary_checks = [
        "python3 tools/test-audit-gba-wifi-link-boundary.py",
        "python3 tools/audit-gba-wifi-link-boundary.py --build-dir build-ci-boundary",
        "python3 tools/audit-gba-wifi-link-boundary.py --binary-only --binary build-ci-android-arm64/mgba_libretro.so",
    ]
    for command in (*fixture_commands, *boundary_checks):
        _required(_compact(text), command)

    return {
        "android_cmake_flags": _cmake_flags(android),
        "android_ndk_version": ndk.group(1),
        "arm_toolchain": {
            "archive": toolchain.get("ARM_TOOLCHAIN_ARCHIVE"),
            "sha256": toolchain.get("ARM_TOOLCHAIN_SHA256"),
        },
        "boundary_checks": boundary_checks,
        "complete_cmake_flags": _cmake_flags(complete),
        "fixture_commands": fixture_commands,
        "focused_cmake_flags": _cmake_flags(focused),
        "focused_targets": focused_targets,
        "focused_test_regex": regex_match.group(1),
        "gate_names": [
            "Complete normal mGBA suite",
            "Focused tests (normal)",
            "Focused tests (ASan + UBSan)",
            "Focused tests (TSan)",
            "Fixture reproducibility",
            "Android arm64 libretro build",
        ],
        "job_ids": job_ids,
        "job_names": job_names,
        "sanitizer_environment": sanitizer_environment,
        "sanitizer_matrix": sanitizer_matrix,
        "triggers": {"pull_request": True, "push_branches": ["master"]},
        "upstream_exception": _upstream_exception(text),
    }


def _validate_reusable_contract(text: str, history: dict[str, object]) -> None:
    workflow_call = (
        "  workflow_call:\n"
        "    inputs:\n"
        "      source_commit:\n"
        "        description: Exact 40-character peeled source commit supplied by a reusable caller\n"
        "        required: false\n"
        "        type: string"
    )
    start = text.find("  workflow_call:\n")
    end = text.find("\n  push:\n", start)
    if start < 0 or end < 0 or text[start:end] != workflow_call:
        raise WorkflowPolicyError("WORKFLOW_CALL")
    if re.search(r"^      (?:command|build_flags|runner|publish)[A-Za-z_-]*:", text, re.MULTILINE):
        raise WorkflowPolicyError("WORKFLOW_CALL")
    permissions = re.search(r"^permissions:\n((?:  [^\n]+\n)+)", text, re.MULTILINE)
    if permissions is None or permissions.group(1) != "  contents: read\n" or re.search(
        r"^    permissions:", text, re.MULTILINE,
    ):
        raise WorkflowPolicyError("WORKFLOW_PERMISSION")

    action_pins = history.get("action_pins")
    if not isinstance(action_pins, dict):
        raise WorkflowPolicyError("WORKFLOW_ACTION_PIN")
    expected = {
        action: f"{action}@{value['sha']}"
        for action, value in action_pins.items()
        if isinstance(value, dict) and isinstance(value.get("sha"), str)
    }
    uses = _USES.findall(text)
    if not uses or any("@" not in value or not _SHA.fullmatch(value.rsplit("@", 1)[1]) for value in uses):
        raise WorkflowPolicyError("WORKFLOW_ACTION_PIN")
    if set(uses) != {expected["actions/checkout"], expected["actions/upload-artifact"]}:
        raise WorkflowPolicyError("WORKFLOW_ACTION_PIN")
    if text.count(expected["actions/checkout"]) != 4:
        raise WorkflowPolicyError("WORKFLOW_ACTION_PIN")
    checkout_and_proof = (
        "      - name: Validate exact source commit\n"
        "        env:\n"
        f"          EXPECTED_SOURCE_COMMIT: {_SOURCE_COMMIT}\n"
        "        run: |\n"
        "          [[ \"$EXPECTED_SOURCE_COMMIT\" =~ ^[0-9a-f]{40}$ ]]\n"
        "\n"
        "      - name: Check out source\n"
        f"        uses: {expected['actions/checkout']} # v6\n"
        "        with:\n"
        f"          ref: {_SOURCE_COMMIT}\n"
        "\n"
        "      - name: Verify checked out source commit\n"
        "        env:\n"
        f"          EXPECTED_SOURCE_COMMIT: {_SOURCE_COMMIT}\n"
        "        run: |\n"
        "          [[ \"$EXPECTED_SOURCE_COMMIT\" =~ ^[0-9a-f]{40}$ ]]\n"
        "          test \"$(git rev-parse HEAD)\" = \"$EXPECTED_SOURCE_COMMIT\"\n"
    )
    if text.count(checkout_and_proof) != 4:
        raise WorkflowPolicyError("WORKFLOW_SOURCE_COMMIT")

    artifact = (
        "      - name: Upload inspected Android ARM64 release inputs\n"
        f"        uses: {expected['actions/upload-artifact']} # v4\n"
        "        with:\n"
        "          name: gba-wifi-link-android-arm64-release-input\n"
        "          path: |\n"
        "            build-ci-android-arm64/mgba_libretro.so\n"
        "            build-ci-android-arm64/BUILD-METADATA.json\n"
        "          if-no-files-found: error\n"
        "          retention-days: 7"
    )
    if artifact not in text or text.count(expected["actions/upload-artifact"]) != 1:
        raise WorkflowPolicyError("WORKFLOW_ARTIFACT")
    boundary = text.find("--binary build-ci-android-arm64/mgba_libretro.so")
    metadata = text.find("- name: Write canonical Android build metadata")
    upload = text.find("- name: Upload inspected Android ARM64 release inputs")
    if boundary < 0 or metadata < boundary or upload < metadata:
        raise WorkflowPolicyError("WORKFLOW_ARTIFACT")
    metadata_step = _step(text, "Write canonical Android build metadata")
    if (
        f"EXPECTED_SOURCE_COMMIT: {_SOURCE_COMMIT}" not in metadata_step
        or 'source_commit = subprocess.run(' not in metadata_step
        or '("git", "rev-parse", "HEAD")' not in metadata_step
        or 'source_commit != os.environ["EXPECTED_SOURCE_COMMIT"]' not in metadata_step
        or '"source_commit": source_commit' not in metadata_step
    ):
        raise WorkflowPolicyError("WORKFLOW_SOURCE_COMMIT")


def validate_workflow_policy(repo: Path) -> None:
    """Reject any drift from protected CI behavior or its constrained handoff."""
    text, history = _read(repo)
    actual = normalize_workflow(repo)
    if actual != history["ci_baseline"]:
        raise WorkflowPolicyError("WORKFLOW_BASELINE")
    _validate_reusable_contract(text, history)
