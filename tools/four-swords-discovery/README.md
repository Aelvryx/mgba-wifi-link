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
before staging a physical run. Automation owns the build, hash, installation,
isolated configuration, logging, monitoring, evidence extraction, teardown,
and cleanup. The human tester owns save selection, game menus, sustained
controller input, gameplay, and audiovisual judgment.

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

After launch, the human presses one physical button on each handheld. Do not
hand off the run until `check-controls` reports the real AYN controller as
`configured in port 1` on both endpoints and a private screenshot proves that
both touchscreen overlays are visible. If either check fails, stop and relaunch
without ADB input injection. Do not improvise controller indices, hotkeys,
menu drivers, or tap sequences.

The stock Android build used here does not expose a working network-command
listener. Host/join is therefore human-owned frontend interaction. Automation
may launch content directly, but must not synthesize a host hotkey or explore
the frontend menus.

After the human confirms the result, run `capture` before `stop`. Verify the
private local evidence, then run `cleanup`; it refuses to operate while
RetroArch is running and removes only the validated run-ID directory beneath
RetroArch's app-specific external storage. Screen sleep remains a physical
power-button action so cleanup never creates a synthetic Android input device.
