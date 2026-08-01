# Four Swords Multi-Pak discovery validation

This document tracks the staged investigation defined by OpenSpec change
`fix-multipak-cable-discovery`. Stage A may collect evidence and add diagnostic
infrastructure, but it may not change production SIO, lockstep, replica, pair,
adapter, session, or wire behavior. Any correction first requires the focused
delta-requirement review at task 4.9.

## Baseline

| Item | Recorded value |
| --- | --- |
| Project baseline | `5b431c0d40edd9ec4e638922aa2519d26ee6c7c6` |
| Alpha release source | `c9b181aa24d5f2136a6e11fca56179b5204555be` (`v0.1.0-alpha.2`) |
| ARM64 core | `build-android-v2-reviewed-arm64/mgba_libretro.so` |
| Core size | 8,043,736 bytes |
| Core SHA-256 | `14978106a3978ab4ef6ec025add82e0e9a38decda72404e3dc8c02b3373f179d` |
| Frontend | Stock RetroArch `1.22.2_GIT`, package `com.retroarch.aarch64` |
| Thor | AYN Thor, ADB `11c5b80`, Android 13; live identity reconfirmed 2026-08-01 |
| Odin | AYN Odin2 Portal, ADB `6986c674`, Android 13; live identity reconfirmed 2026-08-01 |
| Current Four Swords status | Known failure: remains in cable discovery; not yet rerun on topology-settled alpha.2 |

The exact core artifact above is the alpha.2 build under test. Its hash must be
verified on both devices immediately before the run. A live frontend version,
device identity, configuration digest, and installed-core hash belong in the
private run manifest.

## Private qualification boundary

All raw commercial material is confined to the ignored path
`.qualification/four-swords-discovery/<run-id>/` using the layout documented in
`tools/four-swords-discovery/README.md`. This includes ROMs, saves, savestates,
private input scripts, screenshots, and raw logs. Tracked evidence may contain
approved identity hashes and aggregate diagnostics, but never the underlying
commercial data or a production code path selected by its identity.

The tracked manifest template is
`tools/four-swords-discovery/run-manifest.example.json`. The run-specific copy
is private and records the exact build, endpoints, configuration, initial-state
identities where available, time box, success/failure signals, and ownership
checklist before any content starts.

## Initial branch decision

The first run is one bounded physical retest with both devices connected before
the Four Swords cable menu. Automation stages the exact core and isolated
logging, then stops at the expected handoff screen. The human tester performs
all game navigation and gameplay.

- If alpha.2 links, the change adds a non-commercial regression for the
  already-correct topology transition and records the physical qualification.
  Observer infrastructure is a separate decision.
- If alpha.2 still fails, the known-failure status remains and Stage A proceeds
  through the observer and deterministic diagnostic ladder.

No result from this run directly authorizes production behavior changes.

## Android controller preflight correction

The first 2026-08-01 staging attempt used an injected Android key while
RetroArch was running. Android exposed that event as a synthetic `Virtual`
controller, which RetroArch assigned ahead of the Thor's physical controls.
The log showed `Virtual` as the fallback device and the real `Ayn Odin`
controller in port 2. That attempt was stopped before qualification and is not
valid evidence about Four Swords.

The clean rerun introduced no ADB input. Both device-native configurations
were retained, the existing touchscreen overlay was made visible on both
endpoints as a fallback, and the controller gate required these log records
before handoff:

```text
Thor: Ayn Odin configured in port 1.
Odin: Ayn Odin (Xbox Mode) configured in port 1.
```

The checked workflow is
`tools/four-swords-discovery/android-qualification.sh`. Future physical runs
must use its controller gate and must not compensate for a transient virtual
device by changing a joypad index.
