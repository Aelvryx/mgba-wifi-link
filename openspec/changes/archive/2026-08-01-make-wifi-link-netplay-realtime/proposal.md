## Why

The correctness-first distributed SIO protocol is functionally sound but cannot run commercial link-cable games in real time: a two-device qualification run advanced ordinary gameplay at about 29.5 emulated frames per second and completed Four Swords serial words at only 16.7 words per second because each word requires multiple Wi-Fi round trips. The runtime must move serial traffic back onto a local lockstep cable and use the network only for bounded, frame-oriented synchronization.

## What Changes

- Add a replicated-link runtime in which each device runs both GBA players locally through mGBA's existing in-process SIO lockstep coordinator, while displaying, sounding, controlling, and persisting only its assigned player.
- Add a protocol-v2 attachment exchange for quiescent initial snapshots, player assignments, save ownership, input-delay policy, and deterministic state verification.
- Replace per-serial-word `TRANSFER_*` traffic during normal play with frame-numbered input exchange, bounded input rendezvous, and periodic state checksums.
- Prevent empty blocked `retro_run()` calls from starving frontend audio while a peer input or attachment rendezvous is still within its bounded deadline.
- Retain the existing distributed-SIO implementation as a correctness oracle and diagnostic fallback until the replicated runtime passes automated and physical-device qualification.
- Add performance telemetry and acceptance gates for real emulation rate, input latency, packet rate, audio continuity, CPU/thermal load, and local-lockstep serial throughput.
- **BREAKING**: increment the link protocol compatibility version; protocol-v1 and protocol-v2 peers will reject one another rather than attempting a mixed session.

## Capabilities

### New Capabilities

- `gba-replicated-link-runtime`: Defines two locally replicated GBA players, deterministic input synchronization, role-specific presentation and save ownership, state verification, bounded stalls, and real-time performance requirements.

### Modified Capabilities

- `gba-link-netplay-session`: Extends attachment and compatibility negotiation with replicated-state manifests, chunked snapshot transfer, input-delay policy, protocol-v2 lifecycle, and diagnostics.
- `gba-multi-pak-link`: Changes the active network execution model from per-word distributed SIO transactions to local in-process SIO lockstep, while preserving the specified GBA MULTI behavior and failure safety.

## Impact

- Affects the libretro core lifecycle and frame loop, Netpacket codec/session adapter, GBA core construction, local SIO lockstep integration, save-memory routing, input routing, audio/video selection, diagnostics, and focused test infrastructure.
- Adds a second emulated GBA instance per physical device and therefore requires explicit Android CPU, thermal, memory, and battery qualification.
- Keeps stock RetroArch and its Netpacket transport; no RetroArch fork, direct socket layer, relay service, rollback prediction, Single-Pak, RFU, or support beyond two players is introduced by this change.
- Uses the captured two-device Four Swords trace as the performance baseline, qualifies the exact experimental build with a fast-entry commercial title, and records Four Swords as a named compatibility investigation without claiming broad game support.
