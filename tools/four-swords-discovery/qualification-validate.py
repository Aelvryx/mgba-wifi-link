#!/usr/bin/env python3

"""Fail-closed validation for the private Android qualification helper."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


RUN_SCHEMA = "mgba-four-swords-discovery-run-v2"
INSTALLED_HASH_REASON = "APP_PRIVATE_PATH_UNREADABLE"
AUTOCONF_RE = re.compile(r"^\[Autoconf\]\s+(.+?) configured in port ([0-9]+)\.$")
LATENCY_POLICIES = {
    "stable": (1, 2),
    "low_latency": (2, 1),
}
ATTACH_RE = re.compile(
    r"attach P(?P<role>[01]) policy=(?P<policy>[0-9]+) "
    r"delay=(?P<delay>[0-9]+) calibration=(?P<calibration>[0-9]+)ms "
    r"provisional=(?P<provisional>[0-9]+) generation=(?P<generation>[0-9]+)"
)
CALIBRATION_LEGACY_RE = re.compile(
    r"calibration P(?P<role>[01]) provisional=(?P<provisional>[0-9]+) "
    r"generation=(?P<generation>[0-9]+) samples=(?P<samples>[0-9]+) "
    r"min=(?P<minimum>[0-9]+)us p50=(?P<p50>[0-9]+)us "
    r"p95=(?P<p95>[0-9]+)us max=(?P<maximum>[0-9]+)us "
    r"selector=(?P<selector>[0-9]+) floor=(?P<floor>[0-9]+) "
    r"range=(?P<range_min>[0-9]+)-(?P<range_max>[0-9]+) "
    r"delay=(?P<delay>[0-9]+) reason=(?P<reason>[0-9]+) "
    r"digest=(?P<digest>[0-9a-f]{64})"
)
CALIBRATION_IDENTITY_RE = re.compile(
    r"calibration P(?P<role>[01]) provisional=(?P<provisional>[0-9]+) "
    r"generation=(?P<generation>[0-9]+) samples=(?P<samples>[0-9]+)"
)
CALIBRATION_RTT_RE = re.compile(
    r"cal-rtt P(?P<role>[01]) s=(?P<session>[0-9]+) "
    r"min=(?P<minimum>[0-9]+)us "
    r"p50=(?P<p50>[0-9]+)us p95=(?P<p95>[0-9]+)us "
    r"max=(?P<maximum>[0-9]+)us"
)
CALIBRATION_SELECT_RE = re.compile(
    r"cal-select P(?P<role>[01]) s=(?P<session>[0-9]+) "
    r"selector=(?P<selector>[0-9]+) "
    r"floor=(?P<floor>[0-9]+) range=(?P<range_min>[0-9]+)-"
    r"(?P<range_max>[0-9]+) delay=(?P<delay>[0-9]+) "
    r"reason=(?P<reason>[0-9]+)"
)
CALIBRATION_DIGEST_A_RE = re.compile(
    r"cal-digest-a P(?P<role>[01]) s=(?P<session>[0-9]+) "
    r"d=(?P<digest_a>[0-9a-f]{32})"
)
CALIBRATION_DIGEST_B_RE = re.compile(
    r"cal-digest-b P(?P<role>[01]) s=(?P<session>[0-9]+) "
    r"d=(?P<digest_b>[0-9a-f]{32})"
)


class ValidationError(RuntimeError):
    pass


def _fail(message: str) -> None:
    raise ValidationError(message)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        _fail(f"cannot read manifest {path}: {error}")
    if not isinstance(value, dict):
        _fail("manifest root must be an object")
    return value


def _field(value: Any, dotted: str) -> Any:
    current = value
    for component in dotted.split("."):
        if isinstance(current, list):
            try:
                current = current[int(component)]
            except (ValueError, IndexError):
                _fail(f"manifest field is missing: {dotted}")
        elif isinstance(current, dict) and component in current:
            current = current[component]
        else:
            _fail(f"manifest field is missing: {dotted}")
    return current


def _require_equal(actual: Any, expected: Any, label: str) -> None:
    if actual != expected:
        _fail(f"{label} mismatch: expected {expected!r}, got {actual!r}")


def _require_hex(value: Any, label: str, length: int = 64) -> str:
    if not isinstance(value, str) or not re.fullmatch(rf"[0-9a-f]{{{length}}}", value):
        _fail(f"{label} must be a {length}-character lowercase hexadecimal digest")
    return value


def _safe_evidence_path(run_root: Path, relative: Any, label: str) -> Path:
    if not isinstance(relative, str) or not relative:
        _fail(f"{label} must name a run-relative evidence file")
    candidate = (run_root / relative).resolve()
    run_root = run_root.resolve()
    if candidate == run_root or run_root not in candidate.parents:
        _fail(f"{label} escapes the qualification run directory")
    return candidate


def validate_manifest(args: argparse.Namespace) -> None:
    manifest_path = Path(args.manifest).resolve()
    manifest = _load_manifest(manifest_path)
    run_root = manifest_path.parent
    core_path = Path(args.core).resolve()

    _require_equal(_field(manifest, "schema"), RUN_SCHEMA, "manifest schema")
    _require_equal(_field(manifest, "run_id"), args.run_id, "manifest run ID")
    _require_equal(_field(manifest, "source.release_commit"), args.release_commit, "release commit")
    _require_equal(_field(manifest, "source.release_tag"), args.release_tag, "release tag")
    _require_equal(_field(manifest, "core.sha256"), args.core_sha256, "manifest core hash")
    _require_equal(_field(manifest, "core.embedded_version"), args.core_version, "embedded core version")
    _require_equal(_field(manifest, "frontend.name"), "RetroArch", "frontend name")
    _require_equal(_field(manifest, "frontend.version"), args.frontend_version, "frontend version")
    _require_equal(_field(manifest, "frontend.git"), args.frontend_git, "frontend Git identity")
    _require_equal(
        _field(manifest, "frontend.package_version"),
        args.frontend_package_version,
        "frontend package version",
    )
    _require_equal(_field(manifest, "frontend.package"), args.package, "frontend package")
    _require_equal(_field(manifest, "private_inputs.content_crc32"), args.content_crc32, "content CRC32")
    _require_equal(
        _require_hex(_field(manifest, "private_inputs.rom_identity_digest"), "ROM identity digest"),
        args.rom_sha256,
        "approved ROM identity digest",
    )
    policy_value, product_floor = LATENCY_POLICIES[args.latency_policy]
    _require_equal(_field(manifest, "latency.policy"), args.latency_policy, "latency policy")
    _require_equal(_field(manifest, "latency.policy_wire_value"), policy_value, "latency policy wire value")
    _require_equal(_field(manifest, "latency.selector_policy_version"), 1, "selector policy version")
    _require_equal(_field(manifest, "latency.product_floor"), product_floor, "latency product floor")
    _require_equal(_field(manifest, "latency.expected_selected_delay"), args.selected_delay, "selected input delay")

    if not core_path.is_file():
        _fail(f"core artifact is missing: {core_path}")
    _require_equal(_sha256(core_path), args.core_sha256, "core artifact hash")
    core_bytes = core_path.read_bytes()
    if args.release_commit.encode("ascii") not in core_bytes:
        _fail("release commit is not embedded in the core artifact")
    if args.core_version.encode("ascii") not in core_bytes:
        _fail("expected project version is not embedded in the core artifact")

    devices = _field(manifest, "devices")
    if not isinstance(devices, list) or len(devices) != 2:
        _fail("manifest must contain exactly two device records")
    by_name: dict[str, dict[str, Any]] = {}
    for device in devices:
        if not isinstance(device, dict) or device.get("name") not in ("thor", "odin"):
            _fail("device records must be named thor and odin")
        name = device["name"]
        if name in by_name:
            _fail(f"duplicate device record: {name}")
        by_name[name] = device

    expected_devices = {
        "thor": (args.thor_serial, args.thor_controller, "host"),
        "odin": (args.odin_serial, args.odin_controller, "client"),
    }
    for name, (serial, controller, role) in expected_devices.items():
        if name not in by_name:
            _fail(f"missing {name} device record")
        device = by_name[name]
        _require_equal(device.get("serial"), serial, f"{name} serial")
        _require_equal(device.get("expected_controller"), controller, f"{name} controller")
        _require_equal(device.get("role"), role, f"{name} role")
        _require_equal(device.get("staged_core_sha256"), args.core_sha256, f"{name} staged core hash")
        _require_equal(device.get("installed_core_sha256"), None, f"{name} installed core hash")
        _require_equal(
            device.get("installed_core_sha256_reason"),
            INSTALLED_HASH_REASON,
            f"{name} installed core hash reason",
        )
        _require_equal(
            device.get("loaded_core_identity"), args.core_version, f"{name} loaded core identity"
        )
        _require_equal(
            device.get("loaded_core_identity_method"),
            "RETROARCH_CORE_INFORMATION_SCREEN",
            f"{name} loaded core identity method",
        )

        config_path = run_root / "device-snapshots" / f"{name}-qualification.cfg"
        options_path = run_root / "device-snapshots" / f"{name}-mgba-qualification.opt"
        save_path = run_root / "saves" / name / "qualification-pre-run.srm"
        if not config_path.is_file() or not options_path.is_file() or not save_path.is_file():
            _fail(f"{name} qualification config, core options, or isolated save is missing")
        _require_equal(
            _sha256(config_path),
            _require_hex(device.get("configuration_sha256"), f"{name} configuration hash"),
            f"{name} configuration hash",
        )
        _require_equal(
            _sha256(options_path),
            _require_hex(device.get("core_options_sha256"), f"{name} core-options hash"),
            f"{name} core-options hash",
        )
        _require_equal(
            _sha256(save_path),
            _require_hex(device.get("save_sha256"), f"{name} save hash"),
            f"{name} save hash",
        )

        evidence = _safe_evidence_path(
            run_root, device.get("loaded_core_identity_evidence"), f"{name} core identity evidence"
        )
        if not evidence.is_file():
            _fail(f"{name} core identity evidence is missing: {evidence}")
        _require_equal(
            _sha256(evidence),
            _require_hex(
                device.get("loaded_core_identity_evidence_sha256"),
                f"{name} core identity evidence hash",
            ),
            f"{name} core identity evidence hash",
        )


def _effective_config(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        _fail(f"cannot read configuration {path}: {error}")
    values: dict[str, str] = {}
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        key = key.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] == '"':
            value = value[1:-1]
        values[key] = value
    return values


def validate_config(args: argparse.Namespace) -> None:
    values = _effective_config(Path(args.config))
    expected = {
        "input_overlay_enable": "true",
        "input_player1_joypad_index": "0",
        "input_netplay_host_toggle": "nul",
        "savefile_directory": f"{args.remote_root}/saves",
        "savestate_directory": f"{args.remote_root}/states",
        "log_dir": f"{args.remote_root}/logs",
        "config_save_on_exit": "false",
        "autosave_interval": "0",
        "log_to_file": "true",
        "log_to_file_timestamp": "true",
        "global_core_options": "true",
        "core_options_path": f"{args.remote_root}/config/mgba-qualification.opt",
    }
    for key, expected_value in expected.items():
        _require_equal(values.get(key), expected_value, f"effective config value {key}")
    options = _effective_config(Path(args.options))
    _require_equal(
        options.get("mgba_link_netplay_latency"),
        args.latency_policy,
        "effective core option mgba_link_netplay_latency",
    )


def _autoconf_assignments(text: str) -> dict[int, str]:
    ports: dict[int, str] = {}
    device_ports: dict[str, int] = {}
    for line in text.splitlines():
        match = AUTOCONF_RE.match(line.strip())
        if not match:
            continue
        device = match.group(1)
        port = int(match.group(2))
        previous_port = device_ports.get(device)
        if previous_port is not None and ports.get(previous_port) == device:
            del ports[previous_port]
        displaced = ports.get(port)
        if displaced is not None:
            device_ports.pop(displaced, None)
        ports[port] = device
        device_ports[device] = port
    return ports


def validate_runtime_log(args: argparse.Namespace) -> None:
    text = sys.stdin.read().replace("\r", "")
    required = {
        "frontend identity": f"RetroArch {args.frontend_version} (Git {args.frontend_git})",
        "loaded app-private core path": f'[Core] Loading dynamic libretro core from: "{args.internal_core}".',
        "content identity": f"[Content] CRC32: {args.content_crc32}.",
        "GBA Wi-Fi Link registration": (
            "registered mgba-gba-wifi-link using mgba-gba-link-replicated-v2"
        ),
        "isolated save path": f'[Override] Redirecting save file to "{args.remote_root}/saves/',
        "isolated state path": f'[Override] Redirecting save state to "{args.remote_root}/states/',
    }
    for label, needle in required.items():
        if needle not in text:
            _fail(f"runtime log does not prove {label}")

    ports = _autoconf_assignments(text)
    actual = ports.get(1)
    if actual != args.expected_controller:
        details = ", ".join(f"port {port}={device}" for port, device in sorted(ports.items()))
        _fail(
            f"latest effective controller on port 1 must be {args.expected_controller!r}; "
            f"observed {actual!r} ({details or 'no assignments'})"
        )
    if actual.lower().startswith("virtual"):
        _fail("Android Virtual controller must not own RetroArch port 1")

    expected_policy, expected_floor = LATENCY_POLICIES[args.latency_policy]
    attaches = list(ATTACH_RE.finditer(text))
    calibrations = list(CALIBRATION_LEGACY_RE.finditer(text))
    if calibrations:
        calibration = calibrations[-1].groupdict()
    else:
        identities = list(CALIBRATION_IDENTITY_RE.finditer(text))
        rtts = list(CALIBRATION_RTT_RE.finditer(text))
        selections = list(CALIBRATION_SELECT_RE.finditer(text))
        digests_a = list(CALIBRATION_DIGEST_A_RE.finditer(text))
        digests_b = list(CALIBRATION_DIGEST_B_RE.finditer(text))
        if identities and rtts and selections and digests_a and digests_b:
            components = [
                identities[-1].groupdict(),
                rtts[-1].groupdict(),
                selections[-1].groupdict(),
                digests_a[-1].groupdict(),
                digests_b[-1].groupdict(),
            ]
            roles = {component["role"] for component in components}
            if len(roles) != 1:
                _fail("runtime calibration records disagree on endpoint role")
            sessions = {
                component["session"]
                for component in components
                if "session" in component
            }
            if sessions != {components[0]["provisional"]}:
                _fail("runtime calibration records disagree on provisional session")
            calibration = {}
            for component in components:
                calibration.update(
                    {
                        key: value
                        for key, value in component.items()
                        if key != "session"
                    }
                )
            calibration["digest"] = (
                calibration.pop("digest_a") + calibration.pop("digest_b")
            )
        else:
            calibration = None
    if not attaches or calibration is None:
        _fail("runtime log does not contain complete latency calibration evidence")
    attach = attaches[-1].groupdict()
    _require_equal(int(attach["role"]), args.expected_role, "runtime attach endpoint role")
    _require_equal(
        int(calibration["role"]), args.expected_role, "runtime calibration endpoint role"
    )
    _require_equal(attach["role"], calibration["role"], "attach/calibration endpoint role")
    _require_equal(
        attach["provisional"], calibration["provisional"], "attach/calibration provisional ID"
    )
    _require_equal(
        attach["generation"], calibration["generation"], "attach/calibration generation"
    )
    _require_equal(int(attach["policy"]), expected_policy, "runtime latency policy")
    _require_equal(int(attach["delay"]), args.selected_delay, "runtime selected input delay")
    _require_equal(int(calibration["samples"]), 24, "runtime calibration sample count")
    _require_equal(int(calibration["selector"]), 1, "runtime selector policy")
    _require_equal(int(calibration["floor"]), expected_floor, "runtime latency product floor")
    _require_equal(int(calibration["delay"]), args.selected_delay, "calibration selected input delay")
    if int(calibration["range_min"]) > int(calibration["delay"]) or \
            int(calibration["delay"]) > int(calibration["range_max"]):
        _fail("runtime selected input delay lies outside the calibrated range")
    if not (int(calibration["minimum"]) <= int(calibration["p50"]) <=
            int(calibration["p95"]) <= int(calibration["maximum"])):
        _fail("runtime calibration percentiles are malformed")


def manifest_value(args: argparse.Namespace) -> None:
    value = _field(_load_manifest(Path(args.manifest)), args.field)
    if value is None:
        print("null")
    elif isinstance(value, (dict, list)):
        print(json.dumps(value, separators=(",", ":")))
    else:
        print(value)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    manifest = subparsers.add_parser("manifest")
    manifest.add_argument("--manifest", required=True)
    manifest.add_argument("--run-id", required=True)
    manifest.add_argument("--core", required=True)
    manifest.add_argument("--core-sha256", required=True)
    manifest.add_argument("--core-version", required=True)
    manifest.add_argument("--release-commit", required=True)
    manifest.add_argument("--release-tag", required=True)
    manifest.add_argument("--frontend-version", required=True)
    manifest.add_argument("--frontend-git", required=True)
    manifest.add_argument("--frontend-package-version", required=True)
    manifest.add_argument("--package", required=True)
    manifest.add_argument("--content-crc32", required=True)
    manifest.add_argument("--rom-sha256", required=True)
    manifest.add_argument("--thor-serial", required=True)
    manifest.add_argument("--odin-serial", required=True)
    manifest.add_argument("--thor-controller", required=True)
    manifest.add_argument("--odin-controller", required=True)
    manifest.add_argument("--latency-policy", choices=sorted(LATENCY_POLICIES), required=True)
    manifest.add_argument("--selected-delay", type=int, choices=range(1, 9), required=True)
    manifest.set_defaults(handler=validate_manifest)

    config = subparsers.add_parser("config")
    config.add_argument("--config", required=True)
    config.add_argument("--options", required=True)
    config.add_argument("--remote-root", required=True)
    config.add_argument("--latency-policy", choices=sorted(LATENCY_POLICIES), required=True)
    config.set_defaults(handler=validate_config)

    runtime = subparsers.add_parser("runtime-log")
    runtime.add_argument("--frontend-version", required=True)
    runtime.add_argument("--frontend-git", required=True)
    runtime.add_argument("--internal-core", required=True)
    runtime.add_argument("--content-crc32", required=True)
    runtime.add_argument("--remote-root", required=True)
    runtime.add_argument("--expected-controller", required=True)
    runtime.add_argument("--expected-role", type=int, choices=(0, 1), required=True)
    runtime.add_argument("--latency-policy", choices=sorted(LATENCY_POLICIES), required=True)
    runtime.add_argument("--selected-delay", type=int, choices=range(1, 9), required=True)
    runtime.set_defaults(handler=validate_runtime_log)

    value = subparsers.add_parser("manifest-value")
    value.add_argument("--manifest", required=True)
    value.add_argument("--field", required=True)
    value.set_defaults(handler=manifest_value)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        args.handler(args)
    except ValidationError as error:
        print(f"qualification validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
