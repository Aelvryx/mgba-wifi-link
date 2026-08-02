#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
HELPER = TOOLS_DIR / "android-qualification.sh"
VALIDATOR = TOOLS_DIR / "qualification-validate.py"


FAKE_ADB = r'''#!/usr/bin/env python3
import glob
import hashlib
import os
import re
import shlex
import shutil
import sys
from pathlib import Path

root = Path(os.environ["FAKE_ADB_ROOT"])
args = sys.argv[1:]
with (root / "calls.log").open("a", encoding="utf-8") as log:
    log.write(" ".join(args) + "\n")
if len(args) < 3 or args[0] != "-s":
    raise SystemExit(2)
serial = args[1]
command = args[2]
rest = args[3:]
device = root / "devices" / serial
device.mkdir(parents=True, exist_ok=True)

def remote(path):
    if not path.startswith("/") or "/../" in path or path.endswith("/.."):
        raise SystemExit(97)
    return device / path.lstrip("/")

if command == "get-state":
    print("device")
    raise SystemExit(0)
if command == "push":
    source = Path(rest[0])
    target = remote(rest[1])
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    print(f"{source}: 1 file pushed")
    raise SystemExit(0)
if command == "pull":
    source = remote(rest[0])
    target = Path(rest[1])
    target.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, target / source.name, dirs_exist_ok=True)
    else:
        shutil.copyfile(source, target)
    raise SystemExit(0)
if command != "shell":
    raise SystemExit(2)

shell = " ".join(rest)
if shell.startswith("pidof "):
    if (device / "running").exists():
        print("1234")
    raise SystemExit(0)
if shell.startswith("dumpsys package "):
    print(f"versionName={os.environ['EXPECTED_FRONTEND_PACKAGE_VERSION']}")
    raise SystemExit(0)
if shell.startswith("getprop ro.product.model"):
    print("Mock handheld")
    print("13")
    raise SystemExit(0)
if shell.startswith("test -e "):
    path = shlex.split(shell)[2]
    raise SystemExit(0 if remote(path).exists() else 1)
if shell.startswith("test -d "):
    tokens = shlex.split(shell)
    paths = [tokens[index + 1] for index, token in enumerate(tokens[:-1]) if token == "-d"]
    raise SystemExit(0 if paths and all(remote(path).is_dir() for path in paths) else 1)
if shell.startswith("mkdir -p "):
    for path in shlex.split(shell)[2:]:
        remote(path).mkdir(parents=True, exist_ok=True)
    raise SystemExit(0)
if shell.startswith("sha256sum "):
    path = shlex.split(shell)[1]
    target = remote(path)
    if not target.is_file():
        raise SystemExit(1)
    if os.environ.get("FAKE_REMOTE_HASH_MISMATCH") == serial and path.endswith("mgba_libretro_android.so"):
        digest = "0" * 64
    else:
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
    print(f"{digest}  {path}")
    raise SystemExit(0)
if shell.startswith("am force-stop "):
    (device / "running").unlink(missing_ok=True)
    raise SystemExit(0)
if shell.startswith("am start "):
    (device / "running").touch()
    print("Status: ok")
    raise SystemExit(0)
if shell.startswith("ls -1t "):
    match = re.search(r"ls -1t '([^']+)/'\*\.log", shell)
    if not match:
        raise SystemExit(2)
    files = sorted(remote(match.group(1)).glob("*.log"), key=lambda path: path.stat().st_mtime, reverse=True)
    if files:
        relative = "/" + str(files[0].relative_to(device))
        print(relative)
        raise SystemExit(0)
    raise SystemExit(1)
if shell.startswith("cat "):
    path = shlex.split(shell)[1]
    sys.stdout.write(remote(path).read_text(encoding="utf-8"))
    raise SystemExit(0)
if shell.startswith("grep -E "):
    path = shlex.split(shell)[-1]
    for line in remote(path).read_text(encoding="utf-8").splitlines():
        if any(needle in line for needle in ("[Autoconf]", "Found joypad", "registered mgba-gba-wifi-link", "CRC32", "Loading dynamic", "RetroArch ")):
            print(line)
    raise SystemExit(0)
if shell.startswith("rm -rf "):
    path = shlex.split(shell)[2]
    shutil.rmtree(remote(path), ignore_errors=True)
    raise SystemExit(0)
raise SystemExit(2)
'''


class QualificationHelperTest(unittest.TestCase):
    run_id = "test-alpha2-run"
    release_commit = "a" * 40
    release_tag = "v-test-alpha2"
    core_version = "0.11-test-alpha2-aaaaaaa"
    frontend_version = "1.22.2"
    frontend_git = "69a4f0e"
    frontend_package_version = "1.22.2_GIT"
    crc32 = "0x8e91cd13"
    remote_base = "/mock/qualification"
    thor_serial = "thor-test"
    odin_serial = "odin-test"
    latency_policy = "stable"
    selected_delay = 2

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.fake_root = self.root / "adb"
        self.fake_root.mkdir()
        self.fake_adb = self.root / "adb.py"
        self.fake_adb.write_text(FAKE_ADB, encoding="utf-8")
        self.fake_adb.chmod(0o755)
        self.qualification_base = self.root / "qualification"
        self.run_root = self.qualification_base / self.run_id
        self.core = self.root / "mgba.so"
        self.core.write_bytes(
            b"mock core\0" + self.release_commit.encode() + b"\0" + self.core_version.encode() + b"\0"
        )
        self.core_sha = self.sha(self.core)
        self.remote_root = f"{self.remote_base}/{self.run_id}"
        self.create_run_inputs()
        self.env = os.environ.copy()
        self.env.update(
            {
                "ADB": str(self.fake_adb),
                "FAKE_ADB_ROOT": str(self.fake_root),
                "RUN_ID": self.run_id,
                "QUALIFICATION_BASE": str(self.qualification_base),
                "REMOTE_BASE": self.remote_base,
                "CORE_PATH": str(self.core),
                "EXPECTED_RELEASE_COMMIT": self.release_commit,
                "EXPECTED_RELEASE_TAG": self.release_tag,
                "EXPECTED_CORE_SHA256": self.core_sha,
                "EXPECTED_CORE_VERSION": self.core_version,
                "EXPECTED_FRONTEND_VERSION": self.frontend_version,
                "EXPECTED_FRONTEND_GIT": self.frontend_git,
                "EXPECTED_FRONTEND_PACKAGE_VERSION": self.frontend_package_version,
                "EXPECTED_CONTENT_CRC32": self.crc32,
                "EXPECTED_ROM_SHA256": "b" * 64,
                "THOR_SERIAL": self.thor_serial,
                "ODIN_SERIAL": self.odin_serial,
                "EXPECTED_LATENCY_POLICY": self.latency_policy,
                "EXPECTED_SELECTED_DELAY": str(self.selected_delay),
            }
        )

    def tearDown(self) -> None:
        self.temp.cleanup()

    @staticmethod
    def sha(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def config_text(self) -> str:
        return "\n".join(
            [
                'input_overlay_enable = "false"',
                'input_overlay_enable = "true"',
                'input_player1_joypad_index = "0"',
                'input_netplay_host_toggle = "nul"',
                f'savefile_directory = "{self.remote_root}/saves"',
                f'savestate_directory = "{self.remote_root}/states"',
                f'log_dir = "{self.remote_root}/logs"',
                'config_save_on_exit = "false"',
                'autosave_interval = "0"',
                'log_to_file = "true"',
                'log_to_file_timestamp = "true"',
                'global_core_options = "true"',
                f'core_options_path = "{self.remote_root}/config/mgba-qualification.opt"',
                "",
            ]
        )

    def create_run_inputs(self) -> None:
        devices = []
        for name, serial, controller, role in (
            ("thor", self.thor_serial, "Ayn Odin", "host"),
            ("odin", self.odin_serial, "Ayn Odin (Xbox Mode)", "client"),
        ):
            config = self.run_root / "device-snapshots" / f"{name}-qualification.cfg"
            options = self.run_root / "device-snapshots" / f"{name}-mgba-qualification.opt"
            save = self.run_root / "saves" / name / "qualification-pre-run.srm"
            evidence = self.run_root / "screenshots" / f"{name}-installed-core-identity.png"
            config.parent.mkdir(parents=True, exist_ok=True)
            save.parent.mkdir(parents=True, exist_ok=True)
            evidence.parent.mkdir(parents=True, exist_ok=True)
            config.write_text(self.config_text(), encoding="utf-8")
            options.write_text(
                '\n'.join(
                    (
                        f'mgba_link_netplay_latency = "{self.latency_policy}"',
                        '',
                    )
                ),
                encoding="utf-8",
            )
            save.write_bytes(f"{name}-save".encode())
            evidence.write_bytes(f"{name}-identity-evidence".encode())
            devices.append(
                {
                    "name": name,
                    "role": role,
                    "serial": serial,
                    "model": "Mock",
                    "android_version": "13",
                    "expected_controller": controller,
                    "configuration_sha256": self.sha(config),
                    "core_options_sha256": self.sha(options),
                    "save_sha256": self.sha(save),
                    "staged_core_sha256": self.core_sha,
                    "installed_core_sha256": None,
                    "installed_core_sha256_reason": "APP_PRIVATE_PATH_UNREADABLE",
                    "loaded_core_identity": self.core_version,
                    "loaded_core_identity_method": "RETROARCH_CORE_INFORMATION_SCREEN",
                    "loaded_core_identity_evidence": f"screenshots/{name}-installed-core-identity.png",
                    "loaded_core_identity_evidence_sha256": self.sha(evidence),
                }
            )
        manifest = {
            "schema": "mgba-four-swords-discovery-run-v2",
            "run_id": self.run_id,
            "source": {"release_commit": self.release_commit, "release_tag": self.release_tag},
            "core": {"sha256": self.core_sha, "embedded_version": self.core_version},
            "frontend": {
                "name": "RetroArch",
                "version": self.frontend_version,
                "git": self.frontend_git,
                "package_version": self.frontend_package_version,
                "package": "com.retroarch.aarch64",
            },
            "devices": devices,
            "private_inputs": {"rom_identity_digest": "b" * 64, "content_crc32": self.crc32},
            "latency": {
                "policy": self.latency_policy,
                "policy_wire_value": 1,
                "selector_policy_version": 1,
                "product_floor": 2,
                "expected_selected_delay": self.selected_delay,
            },
        }
        (self.run_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

    def run_helper(self, command: str, *, extra_env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        env = self.env.copy()
        if extra_env:
            env.update(extra_env)
        return subprocess.run(
            ["bash", str(HELPER), command], env=env, text=True, capture_output=True, check=False
        )

    def device_path(self, serial: str, remote: str) -> Path:
        return self.fake_root / "devices" / serial / remote.lstrip("/")

    def stage(self) -> None:
        result = self.run_helper("stage")
        self.assertEqual(result.returncode, 0, result.stderr)

    def write_log(
        self,
        serial: str,
        assignments: list[tuple[str, int]],
        *,
        role: int | None = None,
        provisional: int = 9,
        generation: int = 10,
        policy: int = 1,
        floor: int = 2,
        delay: int | None = None,
    ) -> None:
        if delay is None:
            delay = self.selected_delay
        if role is None:
            role = 0 if serial == self.thor_serial else 1
        log = self.device_path(serial, f"{self.remote_root}/logs/retroarch.log")
        log.parent.mkdir(parents=True, exist_ok=True)
        controller_lines = "\n".join(
            f"[Autoconf] {device} configured in port {port}." for device, port in assignments
        )
        log.write_text(
            "\n".join(
                [
                    f"RetroArch {self.frontend_version} (Git {self.frontend_git})",
                    '[Core] Loading dynamic libretro core from: "/data/user/0/com.retroarch.aarch64/cores/mgba_libretro_android.so".',
                    f"[Content] CRC32: {self.crc32}.",
                    f'[Override] Redirecting save file to "{self.remote_root}/saves/mGBA/game.srm".',
                    f'[Override] Redirecting save state to "{self.remote_root}/states/mGBA/game.state".',
                    "Status: GBA Wi-Fi Link: registered mgba-gba-wifi-link "
                    "using mgba-gba-link-replicated-v2",
                    f"attach P{role} policy={policy} delay={delay} calibration=25ms "
                    f"provisional={provisional} generation={generation}",
                    f"calibration P{role} provisional={provisional} generation={generation} "
                    "samples=24",
                    f"cal-rtt P{role} s={provisional} min=1000us p50=2000us "
                    "p95=3000us max=4000us",
                    f"cal-select P{role} s={provisional} selector=1 floor={floor} "
                    f"range=1-8 delay={delay} reason=2",
                    f"cal-digest-a P{role} s={provisional} d={'a' * 32}",
                    f"cal-digest-b P{role} s={provisional} d={'a' * 32}",
                    controller_lines,
                    "",
                ]
            ),
            encoding="utf-8",
        )

    def test_run_id_rejects_dot_and_dotdot_without_adb(self) -> None:
        for run_id in (".", ".."):
            result = self.run_helper("cleanup", extra_env={"RUN_ID": run_id})
            self.assertEqual(result.returncode, 2)
        calls = self.fake_root / "calls.log"
        self.assertFalse(calls.exists() and calls.read_text(encoding="utf-8"))

    def test_existing_remote_run_fails_before_staging(self) -> None:
        self.device_path(self.odin_serial, self.remote_root).mkdir(parents=True)
        result = self.run_helper("stage")
        self.assertNotEqual(result.returncode, 0)
        calls = (self.fake_root / "calls.log").read_text(encoding="utf-8")
        self.assertNotIn(" push ", f" {calls} ")
        self.assertNotIn("mkdir -p", calls)

    def test_remote_hash_mismatch_fails(self) -> None:
        result = self.run_helper("stage", extra_env={"FAKE_REMOTE_HASH_MISMATCH": self.thor_serial})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("remote hash mismatch", result.stderr)

    def test_launch_validates_both_staged_endpoints_before_starting_either(self) -> None:
        self.stage()
        result = self.run_helper("launch", extra_env={"FAKE_REMOTE_HASH_MISMATCH": self.odin_serial})
        self.assertNotEqual(result.returncode, 0)
        calls = (self.fake_root / "calls.log").read_text(encoding="utf-8")
        self.assertNotIn("am start", calls)

    def test_cleanup_targets_only_run_directory(self) -> None:
        for serial in (self.thor_serial, self.odin_serial):
            self.device_path(serial, self.remote_root).mkdir(parents=True)
        result = self.run_helper("cleanup")
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = (self.fake_root / "calls.log").read_text(encoding="utf-8").splitlines()
        removals = [line for line in calls if "rm -rf" in line]
        self.assertEqual(len(removals), 2)
        self.assertTrue(all(self.remote_root in line for line in removals))
        self.assertTrue(all(f"rm -rf '{self.remote_base}'" not in line for line in removals))
        self.assertTrue(all("/.." not in line for line in removals))

    def test_effective_final_config_value_is_enforced(self) -> None:
        thor_config = self.run_root / "device-snapshots/thor-qualification.cfg"
        with thor_config.open("a", encoding="utf-8") as output:
            output.write('savefile_directory = "/normal/saves"\n')
        manifest = json.loads((self.run_root / "manifest.json").read_text(encoding="utf-8"))
        manifest["devices"][0]["configuration_sha256"] = self.sha(thor_config)
        (self.run_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        result = self.run_helper("preflight")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("effective config value savefile_directory", result.stderr)

    def test_run_specific_core_options_must_be_enabled(self) -> None:
        thor_config = self.run_root / "device-snapshots/thor-qualification.cfg"
        with thor_config.open("a", encoding="utf-8") as output:
            output.write('global_core_options = "false"\n')
        manifest = json.loads((self.run_root / "manifest.json").read_text(encoding="utf-8"))
        manifest["devices"][0]["configuration_sha256"] = self.sha(thor_config)
        (self.run_root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        result = self.run_helper("preflight")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("effective config value global_core_options", result.stderr)

    def test_manifest_cannot_attest_a_different_loaded_core(self) -> None:
        manifest_path = self.run_root / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["devices"][0]["loaded_core_identity"] = "some-other-core"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        result = self.run_helper("preflight")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("loaded core identity", result.stderr)

    def test_effective_latency_policy_is_enforced(self) -> None:
        options = self.run_root / "device-snapshots/thor-mgba-qualification.opt"
        with options.open("a", encoding="utf-8") as output:
            output.write('mgba_link_netplay_latency = "low_latency"\n')
        manifest_path = self.run_root / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["devices"][0]["core_options_sha256"] = self.sha(options)
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        result = self.run_helper("preflight")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("mgba_link_netplay_latency", result.stderr)

    def test_controller_and_runtime_gate_accepts_clean_endpoints(self) -> None:
        self.stage()
        self.write_log(self.thor_serial, [("Ayn Odin", 1)])
        self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
        result = self.run_helper("check-controls")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_runtime_gate_rejects_role_reversed_endpoints(self) -> None:
        self.stage()
        for serial, controller, wrong_role in (
            (self.thor_serial, "Ayn Odin", 1),
            (self.odin_serial, "Ayn Odin (Xbox Mode)", 0),
        ):
            with self.subTest(serial=serial):
                self.write_log(self.thor_serial, [("Ayn Odin", 1)])
                self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
                self.write_log(serial, [(controller, 1)], role=wrong_role)
                result = self.run_helper("check-controls")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("endpoint role", result.stderr)

    def test_runtime_gate_rejects_disagreeing_attach_and_calibration_roles(self) -> None:
        self.stage()
        self.write_log(self.thor_serial, [("Ayn Odin", 1)])
        self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
        thor_log = self.device_path(self.thor_serial, f"{self.remote_root}/logs/retroarch.log")
        thor_log.write_text(
            thor_log.read_text(encoding="utf-8").replace("calibration P0", "calibration P1"),
            encoding="utf-8",
        )
        result = self.run_helper("check-controls")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("endpoint role", result.stderr)

    def test_runtime_gate_rejects_stale_role_and_mixed_session_records(self) -> None:
        self.stage()
        self.write_log(self.thor_serial, [("Ayn Odin", 1)])
        self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
        thor_log = self.device_path(self.thor_serial, f"{self.remote_root}/logs/retroarch.log")
        with thor_log.open("a", encoding="utf-8") as output:
            output.write(
                "attach P1 policy=1 delay=2 calibration=25ms "
                "provisional=11 generation=12\n"
            )
        stale_role = self.run_helper("check-controls")
        self.assertNotEqual(stale_role.returncode, 0)
        self.assertIn("endpoint role", stale_role.stderr)

        self.write_log(self.thor_serial, [("Ayn Odin", 1)])
        with thor_log.open("a", encoding="utf-8") as output:
            output.write(
                "attach P0 policy=1 delay=2 calibration=25ms "
                "provisional=11 generation=12\n"
            )
        mixed_session = self.run_helper("check-controls")
        self.assertNotEqual(mixed_session.returncode, 0)
        self.assertIn("attach/calibration provisional ID", mixed_session.stderr)

    def test_runtime_gate_rejects_mixed_calibration_component_sessions(self) -> None:
        self.stage()
        self.write_log(self.thor_serial, [("Ayn Odin", 1)])
        self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
        thor_log = self.device_path(self.thor_serial, f"{self.remote_root}/logs/retroarch.log")
        thor_log.write_text(
            thor_log.read_text(encoding="utf-8").replace(
                "cal-rtt P0 s=9", "cal-rtt P0 s=11"
            ),
            encoding="utf-8",
        )
        result = self.run_helper("check-controls")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("calibration records disagree on provisional session", result.stderr)

    def test_low_latency_prepared_run_is_accepted_when_manifest_matches(self) -> None:
        manifest_path = self.run_root / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["latency"] = {
            "policy": "low_latency",
            "policy_wire_value": 2,
            "selector_policy_version": 1,
            "product_floor": 1,
            "expected_selected_delay": 1,
        }
        for index, name in enumerate(("thor", "odin")):
            options = self.run_root / "device-snapshots" / f"{name}-mgba-qualification.opt"
            options.write_text(
                'mgba_link_netplay_latency = "low_latency"\n',
                encoding="utf-8",
            )
            manifest["devices"][index]["core_options_sha256"] = self.sha(options)
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        override = {
            "EXPECTED_LATENCY_POLICY": "low_latency",
            "EXPECTED_SELECTED_DELAY": "1",
        }
        result = self.run_helper("stage", extra_env=override)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.write_log(self.thor_serial, [("Ayn Odin", 1)], policy=2, floor=1, delay=1)
        self.write_log(
            self.odin_serial,
            [("Ayn Odin (Xbox Mode)", 1)],
            policy=2,
            floor=1,
            delay=1,
        )
        result = self.run_helper("check-controls", extra_env=override)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_controller_gate_rejects_virtual_and_displaced_controller(self) -> None:
        self.stage()
        self.write_log(self.thor_serial, [("Virtual", 1), ("Ayn Odin", 2)])
        self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
        result = self.run_helper("check-controls")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("latest effective controller", result.stderr)

    def test_controller_gate_rejects_absent_expected_controller(self) -> None:
        self.stage()
        self.write_log(self.thor_serial, [])
        self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
        result = self.run_helper("check-controls")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no assignments", result.stderr)

    def test_controller_gate_uses_latest_effective_assignment(self) -> None:
        self.stage()
        self.write_log(self.thor_serial, [("Ayn Odin", 1), ("Virtual", 1)])
        self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
        stale_then_invalid = self.run_helper("check-controls")
        self.assertNotEqual(stale_then_invalid.returncode, 0)

        self.write_log(self.thor_serial, [("Virtual", 1), ("Ayn Odin", 1)])
        clean_relaunch = self.run_helper("check-controls")
        self.assertEqual(clean_relaunch.returncode, 0, clean_relaunch.stderr)

    def test_runtime_frontend_core_and_content_identities_fail_closed(self) -> None:
        self.stage()
        thor_log = self.device_path(self.thor_serial, f"{self.remote_root}/logs/retroarch.log")
        cases = (
            (f"RetroArch {self.frontend_version}", "RetroArch 9.9.9", "frontend identity"),
            (
                "registered mgba-gba-wifi-link using mgba-gba-link-replicated-v2",
                "registered unknown core",
                "GBA Wi-Fi Link registration",
            ),
            (self.crc32, "0x00000000", "content identity"),
        )
        for old, new, expected_error in cases:
            with self.subTest(expected_error=expected_error):
                self.write_log(self.thor_serial, [("Ayn Odin", 1)])
                self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
                thor_log.write_text(
                    thor_log.read_text(encoding="utf-8").replace(old, new), encoding="utf-8"
                )
                result = self.run_helper("check-controls")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)

    def test_runtime_policy_and_selected_delay_fail_closed(self) -> None:
        self.stage()
        thor_log = self.device_path(self.thor_serial, f"{self.remote_root}/logs/retroarch.log")
        cases = (
            ("policy=1", "policy=2", "runtime latency policy"),
            ("delay=2", "delay=3", "runtime selected input delay"),
        )
        for old, new, expected_error in cases:
            with self.subTest(expected_error=expected_error):
                self.write_log(self.thor_serial, [("Ayn Odin", 1)])
                self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
                thor_log.write_text(
                    thor_log.read_text(encoding="utf-8").replace(old, new), encoding="utf-8"
                )
                result = self.run_helper("check-controls")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)

    def test_runtime_missing_or_malformed_calibration_fails_closed(self) -> None:
        self.stage()
        thor_log = self.device_path(self.thor_serial, f"{self.remote_root}/logs/retroarch.log")
        cases = (
            ("calibration P0", "calibration-missing P0", "complete latency calibration evidence"),
            ("p50=2000us p95=3000us", "p50=5000us p95=3000us", "percentiles are malformed"),
        )
        for old, new, expected_error in cases:
            with self.subTest(expected_error=expected_error):
                self.write_log(self.thor_serial, [("Ayn Odin", 1)])
                self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
                thor_log.write_text(
                    thor_log.read_text(encoding="utf-8").replace(old, new), encoding="utf-8"
                )
                result = self.run_helper("check-controls")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)

    def test_latest_log_must_contain_current_calibration(self) -> None:
        self.stage()
        self.write_log(self.thor_serial, [("Ayn Odin", 1)])
        self.write_log(self.odin_serial, [("Ayn Odin (Xbox Mode)", 1)])
        stale = self.device_path(self.thor_serial, f"{self.remote_root}/logs/retroarch.log")
        current = stale.with_name("retroarch-current.log")
        current.write_text(
            stale.read_text(encoding="utf-8").replace("calibration P0", "calibration-missing P0"),
            encoding="utf-8",
        )
        os.utime(current, (stale.stat().st_mtime + 2, stale.stat().st_mtime + 2))
        result = self.run_helper("check-controls")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("complete latency calibration evidence", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
