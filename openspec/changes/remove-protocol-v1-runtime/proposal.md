## Why

The shipped core still exposes the original distributed-cable protocol-v1 runtime even though its per-transfer network barriers make commercial gameplay unusably slow and protocol v2 has replaced it with a qualified replicated-pair architecture. Removing that selectable dead path before the next alpha reduces product ambiguity, maintenance cost, attack surface, and the chance that users accidentally choose an unsupported prototype.

## What Changes

- **BREAKING**: remove the protocol-v1 distributed-SIO runtime, its frontend selector value, wire/session adapter, network SIO driver, v1-only C/Python tests and analyzer, active CI invocation, build targets, and spike configuration from the shipped toolchain.
- Make replicated protocol v2 the sole GBA Wi-Fi link runtime; stale `mgba_gba_link_netplay_runtime` configuration values have no effect and cannot select or negotiate v1.
- Remove documentation and qualification configuration that describe v1 as a selectable diagnostic or rollback runtime.
- Preserve the archived OpenSpec changes and historical qualification evidence that explain v1's design and replacement.
- Preserve common GBA SIO, local lockstep, protocol-v2 failure-semantics, replica, session, and frontend regression coverage; migrate any still-useful distinct generic invariant out of a v1-only test case before deleting that test.
- Keep latency-policy defaults and hotspot evaluation out of this change.

## Capabilities

### New Capabilities

- `gba-link-runtime-selection`: Defines the shipped core's single protocol-v2 Wi-Fi link runtime, stale-option behaviour, no-downgrade rule, and preservation boundary for historical evidence and shared regression coverage.

### Modified Capabilities

- `gba-link-fixed-delay`: Removes the requirement that the obsolete protocol-v1 diagnostic runtime retain selectable wire and timing behaviour; latency-policy versioning applies only to protocol v2.

## Impact

- Removes the v1 implementation under `src/gba/sio/netplay`, its public-internal headers, the libretro `netpacket.c` adapter, v1-only test programs, and their build-system entries where ownership is confirmed during implementation.
- Simplifies libretro runtime selection and core options so GBA Netpacket attachment always uses `netpacket-v2`.
- Removes the active v1 packet-log analyzer, its self-test/CI invocation, and v1-only spike configurations while retaining recorded historical results and Git history.
- Updates README, roadmap, qualification configuration, provenance, and developer documentation that mention selectable v1.
- Changes the shipped core-option surface and intentionally breaks compatibility with attempts to select or connect the retired v1 protocol; protocol-v2 wire compatibility and latency policy are unchanged.
