## Why

Protocol v2 now delivers real-time two-player Multi-Pak gameplay, but it still assumes that both endpoints have equivalent timing-sensitive emulator configuration and external inputs. Its fixed input delay is also selected from one contaminated attachment measurement, a hard-coded jitter allowance, and an extra whole-frame guard, causing clean same-LAN sessions to carry four or five frames of latency when less buffering may be sufficient.

## What Changes

- Negotiate a versioned determinism profile before replica exchange, covering peer-equal timing-sensitive core configuration, BIOS/HLE policy, RTC-normalization policy, cheats, input-direction policy, and cartridge-required authoritative inputs while negotiating each endpoint's RTC and synchronized-input capabilities separately and retaining each logical player's RTC source as player-owned state.
- Fail closed with an actionable diagnostic when peers differ on deterministic configuration or when a cartridge requires an external input source that protocol v2 cannot yet synchronize.
- Establish deterministic RTC normalization for replicated sessions on peers with canonical signed-64-bit time semantics, while rejecting RTC-bearing sessions before calibration on narrower platforms instead of allowing endpoint wall clocks to diverge.
- Replace attachment-duration-derived input delay with dedicated, multi-sample network timing calibration that excludes snapshot capture, compression, and replica installation work.
- Add versioned fixed-delay selection and telemetry based on measured transit variation and scheduling guard, permitting a one-frame delay only when the exact unpublished release candidate passes its evidence gate and otherwise retaining the stable two-frame product floor.
- Record input arrival lead, deadline misses, rendezvous causes, per-endpoint p95 and maximum wait tails, and the rationale for the selected delay so latency can be tuned without weakening deterministic execution.
- Preserve authoritative fixed-delay lockstep: late input blocks within the existing bounded deadline and is never predicted, repeated, discarded, or retroactively applied.
- **BREAKING**: increment the experimental protocol-v2 runtime compatibility version and reject older peers rather than silently mixing compatibility or latency policies.
- Explicitly defer rollback, prediction, run-ahead, zero-frame network input, dynamic delay changes during play, synchronized sensor payloads, and four-player support.

## Capabilities

### New Capabilities

- `gba-link-determinism-profile`: Defines deterministic configuration negotiation, replicated RTC policy, unsupported external-input rejection, compatibility diagnostics, and live-session configuration immutability.
- `gba-link-fixed-delay`: Defines clean network calibration, versioned fixed input-delay selection, fixed-session input mapping, latency telemetry, fail-closed calibration behavior, and evidence-based acceptance gates.

### Modified Capabilities

- None.

## Impact

- Affects protocol-v2 HELLO/session negotiation, runtime compatibility versioning, replica initialization, RTC handling, cartridge/input capability checks, input-delay selection, libretro diagnostics, test transports, documentation, and Android qualification evidence.
- Does not change mGBA's local SIO cable semantics, the replicated-pair scheduler, save ownership, commercial-ROM handling, stock RetroArch, or the Netpacket transport contract.
- Requires codec, session, deterministic replay, fault-injection, full-suite, sanitizer, Android ARM64 build, continuous fixture, and focused physical latency qualification before becoming the next experimental alpha baseline.
