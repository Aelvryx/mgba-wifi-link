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
| P0 | Android 13 host endpoint; live role and platform identity reconfirmed 2026-08-01 |
| P1 | Android 13 client endpoint; live role and platform identity reconfirmed 2026-08-01 |
| Current Four Swords status | Verified: topology-settled alpha.2 completed discovery and brief shared gameplay |

The exact artifact above was hash-verified in each app-specific staging
directory. The stock Android release package does not allow ADB to read the
loaded private core path directly. The completed run recorded that limitation
textually rather than inventing a hash. The hardened v2 manifest makes the
contract machine-readable as `installed_core_sha256: null` with reason
`APP_PRIVATE_PATH_UNREADABLE`. A human-owned Core Information record supplies
the loaded embedded identity; automation validates its evidence digest, the
staged artifact's embedded commit/version, the runtime-loaded app-private path,
and protocol-v2 registration. These are separate custody facts.

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

## Alpha.2 qualification result

Run `20260801-110213-alpha2-four-swords` connected the host before the Four
Swords cable menu and joined the client as player two. Both devices left
discovery, displayed both logical players in the same shared game, accepted
their respective physical controls, and retained normal animation and audio
during the playtesting window. Final screenshots independently show the same
two-player room from each role.

The strict commercial-log analyzer reported:

```text
frames=27000 checks=449/450 packets=27482/27481
serial=110852/221704 audio_empty=0/0 fps=60.277/60.278
rv_p50=31/0ms rv_p95=52/0ms rv_max=63/0ms
packet_rate=61.353/61.352pps byte_rate=7782.0/7781.9Bps
lead=1/1 trace_samples=45
```

All 45 sampled P0/P1 trace pairs matched. Neither endpoint logged a protocol,
SIO, timeout, or divergence failure, and neither produced an empty-audio
frame. Both isolated post-run saves matched their pre-run digest, so the run
did not mutate persistent user data. RetroArch was stopped on both endpoints,
the run-specific device directories were removed after capture, and raw
commercial evidence remains only in the ignored private run directory.

| Private evidence | SHA-256 |
| --- | --- |
| P0 core log | `35188a76adc949ca894492628c6d24cb35f61f876b2ed9ae20a8a2720a28eadc` |
| P1 core log | `caed28d74130d2be33c78fc647b97da25087d2b7fabc1db0fae50d70c1d088dd` |
| P0 terminal screenshot | `53e9f758ae10856b132a1c4628ae365301efcf954f60be3183164ce126dba3b6` |
| P1 terminal screenshot | `b006fa9beef9d98b9f4500907f7a73b62c4830625a82b888715f46229513c632` |
| Both pre/post isolated saves | `8897fe438b05596b4852cb5a8cfc38305e1f61b027571bb1f7f4267d23179627` |

The already-present redistributable regression
`detachedMultiSnapshotsExposeAttachedLinesBeforeExecution` was strengthened to
read SIOCNT and RCNT through the guest-visible I/O path before either logical
CPU executes. It proves that detached MULTI snapshots become ready P0/P1
topologies immediately after pair installation. The complete 14-case
`test-gba-replicated-pair` executable passes normally, under ASan/UBSan with
leak detection, and under TSan with that assertion. The complete 17-executable
focused suite also passes normally.

The baseline-success branch therefore introduces no observer, comparator, or
production behavior change. Permanent transition tracing may be considered as
a separate diagnostics feature if future compatibility evidence justifies its
cost.

## Android controller preflight correction

The first 2026-08-01 staging attempt used an injected Android key while
RetroArch was running. Android exposed that event as a synthetic `Virtual`
controller, which RetroArch assigned ahead of the host endpoint's physical
controls. The log showed `Virtual` as the fallback device and the expected
physical controller in port 2. That attempt was stopped before qualification
and is not valid evidence about Four Swords.

The clean rerun introduced no ADB input. Both device-native configurations
were retained, the existing touchscreen overlay was made visible on both
endpoints as a fallback, and the controller gate required each endpoint's
manifest-declared physical controller on port 1 before handoff.

The checked workflow is
`tools/four-swords-discovery/android-qualification.sh`. Future physical runs
must use its hardened latest-effective-assignment gate, which explicitly
rejects `Virtual` on port 1, and must not compensate for a transient virtual
device by changing a joypad index. The successful baseline evidence was
manually checked for the same endpoint-specific records; helper hardening does
not alter or require repeating that gameplay result.
