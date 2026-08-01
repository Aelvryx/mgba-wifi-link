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
