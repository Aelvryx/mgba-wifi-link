# Four Swords discovery qualification

This directory contains only redistributable templates and tooling for the
`fix-multipak-cable-discovery` OpenSpec change. Raw commercial content and
human-owned test material belong under the ignored local directory:

```text
.qualification/four-swords-discovery/<run-id>/
├── content/       ROM copies used only for the private run
├── saves/         isolated save files
├── states/        private initial savestates
├── inputs/        private per-frame input scripts
├── logs/          raw RetroArch, core, logcat, and monitor output
├── screenshots/   private visual evidence
└── manifest.json  private run manifest
```

Only approved cryptographic digests, aggregate measurements, causal findings,
and reproducible non-commercial regressions may be copied into tracked
documentation. A digest identifies evidence; it must never select production
behavior.

Copy `run-manifest.example.json` to the ignored run directory and fill it in
before staging a physical run. `RUN_ID` is a single 1–64 character path
component that begins and ends with a letter or digit. The helper refuses `.`
and `..`, existing device run directories, path escapes, duplicate-config
ambiguity, or a staged hash mismatch.

Each endpoint also needs a run-owned
`device-snapshots/<name>-mgba-qualification.opt`. It must select
`mgba_gba_link_netplay_runtime = "replicated-v2"` and the policy named by the
manifest (`mgba_link_netplay_latency = "stable"` or `"low_latency"`). Invoke
the helper with matching `EXPECTED_LATENCY_POLICY` and
`EXPECTED_SELECTED_DELAY` values. The helper stages and hashes this file and
then requires the latest runtime log to prove the same policy, 24-sample
calibration, selector version, product floor, and selected delay. A stale or
incomplete log cannot qualify a run.

Android does not let ADB read RetroArch's app-private installed core. Install
the exact staged artifact first through RetroArch's human-owned core installer,
open Core Information, and preserve a private screenshot showing the embedded
version from the manifest. Record `installed_core_sha256` as JSON `null` and
`installed_core_sha256_reason` as `APP_PRIVATE_PATH_UNREADABLE`; never invent a
hash. The helper verifies the staged artifact's hash and embedded commit,
verifies that evidence file and its digest, then proves at runtime that
RetroArch loaded the expected app-private core path and that it registered the
replicated-pair v2 interface. These are deliberately distinct custody facts.

Automation owns build verification, hashes, isolated configuration, logging,
monitoring, evidence extraction, teardown, and cleanup. Human ownership is
limited to installing/confirming the app-private core, save selection, game
menus, sustained controller input, gameplay, and audiovisual judgment.

The initial alpha.2 retest is a hard branch point. If Four Swords links, add a
non-commercial topology regression and finish the baseline-success path. If it
does not, retain the failure evidence and begin Stage A diagnostics. No
production behavior may change until task 4.9 has converted trace evidence
into a focused, reviewed delta requirement.

## Canonical Android control setup

Use `android-qualification.sh` for device staging, launch, control validation,
capture, stop, and removal of its run-specific device directory. The workflow
deliberately contains no `adb shell input`
command. Injecting a key or touch through ADB registers Android's synthetic
`Virtual` controller; RetroArch can assign it to port 1 and move the handheld's
real controls to port 2. Changing `input_player1_joypad_index` to compensate is
not a fix because the synthetic device is transient.

Every qualification config must clone that device's current normal config and
change only the isolated save/state/log paths, autosave/config persistence, and
explicitly documented diagnostic options. It must retain normal input, joypad,
overlay path, menu, video, and audio settings, while setting
`input_overlay_enable = "true"` as a visible fallback on both devices.
The helper interprets the last occurrence of every config key, matching
RetroArch's effective value. It requires run-specific save, state, and log
directories, disabled config-save-on-exit and autosave, timestamped file
logging, a run-specific `core_options_path`, native joypad index 0, and no
injected host hotkey. It also requires `global_core_options = "true"`; without
that setting RetroArch may ignore the run-specific options file in favour of a
per-core file from the normal configuration tree. This prevents the
qualification launch from persisting
core-option changes into the normal RetroArch configuration tree.

After launch, the human presses one physical button on each handheld. Do not
hand off the run until `check-controls` confirms the latest effective
assignment is `Ayn Odin` on Thor port 1 and `Ayn Odin (Xbox Mode)` on Odin port
1, and a private screenshot proves that both touchscreen overlays are visible.
`Virtual` on port 1, an AYN controller displaced to a later port, or a stale
historical assignment all fail closed. The same check also verifies the exact
RetroArch build, content CRC, app-private core path, v2 registration, isolated
runtime paths, and remote hashes. If it fails, stop and relaunch without ADB
input injection. Do not improvise controller indices, hotkeys, menu drivers,
or tap sequences.

The stock Android build used here does not expose a working network-command
listener. Host/join is therefore human-owned frontend interaction. Automation
may launch content directly, but must not synthesize a host hotkey or explore
the frontend menus.

After the human confirms the result, run `capture` before `stop`. Verify the
private local evidence, then run `cleanup`; it refuses to operate while
RetroArch is running and removes only the validated run-ID directory beneath
RetroArch's app-specific external storage. Screen sleep remains a physical
power-button action so cleanup never creates a synthetic Android input device.
