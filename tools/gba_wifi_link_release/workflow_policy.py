"""Fail-closed static policy checks for the protected CI workflow.

The project intentionally avoids a YAML parser here.  The workflow is a small,
reviewed source contract, so these checks validate its exact, security-relevant
spelling and normalize only the behavioral fields frozen in the history fixture.
"""

import hashlib
import json
from pathlib import Path
import re


_WORKFLOW = Path(".github/workflows/gba-wifi-link-ci.yml")
_RELEASE_WORKFLOW = Path(".github/workflows/gba-wifi-link-release.yml")
_RELEASE_CONTRACT = Path("packaging/gba-wifi-link/release/contract-v1.json")
_HISTORY = Path("tools/gba_wifi_link_release/fixtures/v0.2.0-history.json")
_SHA = re.compile(r"^[0-9a-f]{40}$")
_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_USES = re.compile(r"^\s*uses:\s*([^\s#]+)(?:\s+#.*)?\s*$", re.MULTILINE)
_SOURCE_COMMIT = "${{ inputs.source_commit || github.sha }}"
_PERMISSIONS_KEY = re.compile(
    r"(?:^|[,{])[^\S\r\n]*(?:permissions|['\"]permissions['\"])[^\S\r\n]*:",
    re.MULTILINE,
)
_PLAIN_KEY = re.compile(r"^(?P<key>[A-Za-z_][A-Za-z0-9_-]*)[ \t]*:(?P<value>.*)$")
_BLOCK_SCALAR = re.compile(r"^[>|][0-9+\-]*?(?:\s*(?:#.*)?)$")


class WorkflowPolicyError(ValueError):
    """A stable reason that a workflow source contract was violated."""


def lex_workflow_yaml(text: str) -> tuple[str, ...]:
    """Lex the deliberately plain YAML subset used by the reviewed workflow.

    This is not a general YAML parser. It rejects syntax that could change key
    meaning (complex/quoted keys, tags, anchors, aliases, merge keys, and flow
    maps), while treating literal and folded block-scalar bodies as opaque.
    """
    tokens: list[str] = []
    scalar_indent: int | None = None
    for line in text.splitlines():
        leading = len(line) - len(line.lstrip(" "))
        if scalar_indent is not None:
            if not line.strip() or leading > scalar_indent:
                continue
            scalar_indent = None
        if "\t" in line:
            raise WorkflowPolicyError("WORKFLOW_YAML_SYNTAX")
        content = line[leading:]
        if not content or content.startswith("#"):
            continue
        is_list = content.startswith("- ")
        item = content[2:] if is_list else content
        if item.startswith(("?", ":", "'", '"', "!", "&", "*", "<<:")):
            raise WorkflowPolicyError("WORKFLOW_YAML_SYNTAX")
        key = _PLAIN_KEY.fullmatch(item)
        if key is None:
            if is_list:
                tokens.append(f"L:{leading}:{item}")
                continue
            raise WorkflowPolicyError("WORKFLOW_YAML_SYNTAX")
        value = key.group("value")
        value_without_expression = re.sub(r"\$\{\{.*?\}\}", "", value)
        if (
            "{" in value_without_expression
            or "}" in value_without_expression
            or re.search(r"(?:^|[\s:])[!&*][^\s]*", value_without_expression)
        ):
            raise WorkflowPolicyError("WORKFLOW_YAML_SYNTAX")
        tokens.append(f"M:{leading}:{'-' if is_list else ''}{key.group('key')}")
        if _BLOCK_SCALAR.fullmatch(value.strip()):
            scalar_indent = leading
    return tuple(tokens)


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
    permission_keys = list(_PERMISSIONS_KEY.finditer(text))
    canonical_permissions = "permissions:\n  contents: read\n\nconcurrency:"
    if (
        len(permission_keys) != 1
        or permission_keys[0].group(0) != "permissions:"
        or not text[permission_keys[0].start():].startswith(canonical_permissions)
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


def _validate_yaml_subset(text: str, history: dict[str, object]) -> None:
    expected = history.get("workflow_yaml_lexical_sha256")
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise WorkflowPolicyError("WORKFLOW_YAML_SYNTAX")
    tokens = lex_workflow_yaml(text)
    actual = hashlib.sha256(("\n".join(tokens) + "\n").encode("utf-8")).hexdigest()
    if actual != expected:
        raise WorkflowPolicyError("WORKFLOW_YAML_SYNTAX")


def validate_workflow_policy(repo: Path) -> None:
    """Reject any drift from protected CI behavior or its constrained handoff."""
    text, history = _read(repo)
    _validate_yaml_subset(text, history)
    actual = normalize_workflow(repo)
    if actual != history["ci_baseline"]:
        raise WorkflowPolicyError("WORKFLOW_BASELINE")
    _validate_reusable_contract(text, history)


def _read_release_workflow(repo: Path) -> tuple[str, dict[str, object]]:
    try:
        text = (repo / _RELEASE_WORKFLOW).read_text(encoding="utf-8")
        contract = json.loads((repo / _RELEASE_CONTRACT).read_text(encoding="utf-8"))
        workflow = contract["release_workflow"]
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_INPUT") from error
    if not isinstance(workflow, dict):
        raise WorkflowPolicyError("RELEASE_WORKFLOW_CONTRACT")
    return text, workflow


def _release_required(text: str, fragment: str, category: str) -> None:
    if fragment not in text:
        raise WorkflowPolicyError(category)


def validate_release_workflow_policy(repo: Path) -> None:
    """Reject release automation that weakens the reviewed fail-closed graph."""
    text, contract = _read_release_workflow(repo)
    expected_source_fingerprint = contract.get("yaml_source_sha256")
    source_fingerprint = hashlib.sha256(text.encode("utf-8")).hexdigest()
    if (
        not isinstance(expected_source_fingerprint, str)
        or not _SHA256.fullmatch(expected_source_fingerprint)
        or expected_source_fingerprint != source_fingerprint
    ):
        raise WorkflowPolicyError("RELEASE_WORKFLOW_SOURCE")
    tokens = lex_workflow_yaml(text)
    expected_fingerprint = contract.get("yaml_lexical_sha256")
    fingerprint = hashlib.sha256(("\n".join(tokens) + "\n").encode("utf-8")).hexdigest()
    if expected_fingerprint != fingerprint:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_YAML")

    trigger = "on:\n  push:\n    tags:\n      - v*\n"
    if text.count(trigger) != 1 or "workflow_dispatch" in text:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_TRIGGER")
    concurrency = (
        "concurrency:\n"
        "  group: gba-wifi-link-release-${{ github.ref }}\n"
        "  cancel-in-progress: false"
    )
    _release_required(text, concurrency, "RELEASE_WORKFLOW_CONCURRENCY")
    if "environment:" in text:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_MANUAL_GATE")

    permission_keys = list(_PERMISSIONS_KEY.finditer(text))
    top_permissions = "permissions:\n  actions: read\n  contents: read\n\nconcurrency:"
    publisher_permissions = (
        "    permissions:\n"
        "      attestations: write\n"
        "      contents: write\n"
        "      id-token: write\n"
        "    steps:"
    )
    if (
        len(permission_keys) != 2
        or not text[permission_keys[0].start():].startswith(top_permissions)
        or not text[permission_keys[1].start():].startswith(publisher_permissions)
    ):
        raise WorkflowPolicyError("RELEASE_WORKFLOW_PERMISSION")

    expected_jobs = [
        "inspect-tag", "protected-validation", "admit", "protected-build",
        "independent-build", "compare-builds", "package", "publish",
    ]
    jobs = re.findall(r"^  ([a-z0-9-]+):\n    name:", text, re.MULTILINE)
    if jobs != expected_jobs:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_GRAPH")
    graph = (
        "  protected-validation:\n    name: Protected exact-commit validation\n    needs: inspect-tag\n",
        "  admit:\n    name: Admit protected release source\n    needs: [inspect-tag, protected-validation]\n",
        "  protected-build:\n    name: Build first clean Android core\n    needs: [inspect-tag, admit]\n",
        "  independent-build:\n    name: Build independent clean Android core\n    needs: [inspect-tag, admit]\n",
        "  compare-builds:\n    name: Compare independent Android builds\n    needs: [inspect-tag, admit, protected-build, independent-build]\n",
        "  package:\n    name: Build and verify deterministic release twice\n    needs: [inspect-tag, compare-builds]\n",
        "  publish:\n    name: Attest and automatically publish\n    needs: [admit, package]\n",
    )
    if any(fragment not in text for fragment in graph):
        raise WorkflowPolicyError("RELEASE_WORKFLOW_GRAPH")

    action_pins = contract.get("action_pins")
    if not isinstance(action_pins, dict) or set(action_pins) != {
        "actions/attest-build-provenance", "actions/checkout",
        "actions/download-artifact", "actions/upload-artifact",
    }:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_ACTION_PIN")
    expected_actions = {
        name: f"{name}@{digest}" for name, digest in action_pins.items()
        if isinstance(name, str) and isinstance(digest, str) and _SHA.fullmatch(digest)
    }
    if len(expected_actions) != 4:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_ACTION_PIN")
    uses = _USES.findall(text)
    third_party = [value for value in uses if not value.startswith("./")]
    if (
        set(third_party) != set(expected_actions.values())
        or third_party.count(expected_actions["actions/checkout"]) != 5
        or third_party.count(expected_actions["actions/download-artifact"]) != 6
        or third_party.count(expected_actions["actions/upload-artifact"]) != 5
        or third_party.count(expected_actions["actions/attest-build-provenance"]) != 2
    ):
        raise WorkflowPolicyError("RELEASE_WORKFLOW_ACTION_PIN")

    artifact_name = contract.get("artifact_name")
    entries = contract.get("artifact_entries")
    if artifact_name != "gba-wifi-link-release-canonical" or entries != [
        "release", "release-body.md", "release-context.json", "release-tool",
    ]:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_ARTIFACT")
    canonical_upload = (
        "      - name: Upload one immutable canonical publisher artifact\n"
        f"        uses: {expected_actions['actions/upload-artifact']} # v4\n"
        "        with:\n"
        f"          name: {artifact_name}\n"
        "          path: publisher-input\n"
        "          if-no-files-found: error\n"
        "          retention-days: 7"
    )
    canonical_download = (
        "      - name: Download canonical publisher input\n"
        f"        uses: {expected_actions['actions/download-artifact']} # v5\n"
        "        with:\n"
        f"          name: {artifact_name}\n"
        "          path: publisher-input"
    )
    if canonical_upload not in text or canonical_download not in text or text.count(artifact_name) != 2:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_ARTIFACT")
    if "if-no-files-found: warn" in text or text.count("if-no-files-found: error") != 5:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_ARTIFACT")

    reproducibility_guards = (
        "cmake --build build-release-android --parallel 2 --target mgba_libretro",
        '-DGIT_COMMIT="$EXPECTED_COMMIT"',
        '-DGIT_TAG="$EXPECTED_TAG"',
        "sha256sum --check build-digests.txt",
        'test "$protected_sha" = "$independent_sha"',
        "cmp --silent build-protected/mgba_libretro.so build-independent/mgba_libretro.so",
        "diff --recursive --no-dereference package-a/release package-b/release",
        "verify --context context.json --output package-a/release",
        "verify --context context.json --output package-b/release",
        "cmp --silent release-body-a.md release-body-b.md",
    )
    if (
        any(fragment not in text for fragment in reproducibility_guards)
        or text.count("cmake -S . -B build-release-android -G Ninja") != 2
        or text.count("cmake --build build-release-android --parallel 2 --target mgba_libretro") != 2
        or text.count("-DSKIP_GIT=ON") != 2
        or text.count('-DGIT_COMMIT="$EXPECTED_COMMIT"') != 2
        or text.count('-DGIT_TAG="$EXPECTED_TAG"') != 2
        or text.count('SOURCE_DATE_EPOCH="$(git show -s --format=%ct "$EXPECTED_COMMIT")"') != 2
        or text.count("python3 tools/gba-wifi-link-release.py build") != 2
    ):
        raise WorkflowPolicyError("RELEASE_WORKFLOW_REPRODUCIBILITY")

    gate_fragment = (
        "          required = (\n"
        "              \"Complete normal mGBA suite\",\n"
        "              \"Focused tests (normal)\",\n"
        "              \"Focused tests (ASan + UBSan)\",\n"
        "              \"Focused tests (TSan)\",\n"
        "              \"Fixture reproducibility\",\n"
        "              \"Android arm64 libretro build\",\n"
        "          )"
    )
    failure_guards = (
        gate_fragment,
        'test "$release_status" -eq 1',
        '"release": {"exists": release_exists}',
        "cut -c67- publisher-input/release-tool/MANIFEST.sha256",
        "sha256sum --check publisher-input/release-tool/MANIFEST.sha256",
        "--allow-existing-release",
        "release_exists: ${{ steps.evidence.outputs.release_exists }}",
    )
    if any(fragment not in text for fragment in failure_guards):
        raise WorkflowPolicyError("RELEASE_WORKFLOW_FAILURE_GUARD")

    publisher_start = text.find("\n  publish:\n")
    if publisher_start < 0:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_PUBLISHER")
    publisher = text[publisher_start:]
    if publisher.count("if: ${{ needs.admit.outputs.release_exists != 'true' }}") != 2:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_RERUN")
    publisher_uses = _USES.findall(publisher)
    if publisher_uses != [
        expected_actions["actions/download-artifact"],
        expected_actions["actions/attest-build-provenance"],
        expected_actions["actions/attest-build-provenance"],
    ]:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_PUBLISHER")
    forbidden = (
        "actions/checkout@", "cmake", "ninja", "make ", "sdkmanager", "apt-get",
        "render-body", "curl ", "wget ", "continue-on-error:", "if: always()",
        " cp ", " mv ",
    )
    if any(value in publisher for value in forbidden):
        raise WorkflowPolicyError("RELEASE_WORKFLOW_PUBLISHER")
    order = [
        publisher.find("- name: Verify canonical release before mutation"),
        publisher.find("- name: Recheck immutable remote tag"),
        publisher.find("- name: Attest admitted core"),
        publisher.find("- name: Attest admitted archive"),
        publisher.find("- name: Publish transaction and final public verification"),
    ]
    if any(index < 0 for index in order) or order != sorted(order):
        raise WorkflowPolicyError("RELEASE_WORKFLOW_PUBLISHER_ORDER")
    if " verify-tag " not in publisher or publisher.count(" publish ") != 1:
        raise WorkflowPolicyError("RELEASE_WORKFLOW_TAG_RECHECK")
