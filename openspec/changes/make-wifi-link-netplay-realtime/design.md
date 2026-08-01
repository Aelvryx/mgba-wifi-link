## Context

The protocol-v1 implementation distributes the GBA cable itself. Player zero leads virtual time and every MULTI word passes through `TRANSFER_START`, `TRANSFER_READY`, `TRANSFER_COMMIT`, completion catch-up, completion decision, and final acknowledgement barriers. This produces deterministic hardware behavior and strong failure handling, but the cable becomes limited by Wi-Fi latency rather than the emulated GBA serial clock.

A two-device qualification run on the Odin/Thor pair measured two distinct problems:

- With no active cable traffic, each single-flight frame grant covered 280,896 GBA cycles and took a mean of about 28.75 ms to acknowledge. The GBA advanced at about 29.5 frames per second while RetroArch called `retro_run()` at about 120 calls per second. Blocked calls returned cached video with little or no audio, causing frequent Android `AudioTrack` stop/restart behavior and badly warped audio.
- During the Four Swords linking screen, 2,803 successful serial words took 167.8 seconds: 16.7 words per second. One successful word required roughly three network round trips and completed in a median 44 ms. No timeout or protocol fault occurred; the design was operating correctly and was still functionally unplayable.

The second result cannot be repaired by tuning the grant interval or combining a few messages. A physical MULTI transfer completes in hundreds or thousands of GBA cycles, so any generic design that waits on Wi-Fi for each dependent word remains orders of magnitude too slow. The existing in-process `GBASIOLockstepCoordinator` already executes this cable traffic at emulated speed between local cores.

Stock RetroArch remains the Android frontend and Netpacket transport. Each device loads the same effective ROM for the MVP, but each player may have different save memory and a different live machine state when connecting. The feature therefore needs to reproduce both cartridges locally without exposing or persisting the shadow cartridge as the user's own game.

## Goals / Non-Goals

**Goals:**

- Sustain real-time two-player GBA Multi-Pak play on the tested same-LAN Android class without making network round trips per serial word.
- Preserve generic mGBA SIO behavior by running every cable transfer through the existing local lockstep implementation.
- Reproduce the same pair of emulated machines on both physical devices from an agreed attachment boundary.
- Exchange only frame-numbered inputs and verification data during ordinary play.
- Present video/audio and accept controls only for the device's assigned player.
- Persist only the assigned player's save data back to that device's normal save memory.
- Bound all attachment and input waits, preserve callback-generation safety, and fail closed on divergence or transport loss.
- Make measured real-time, audio, packet-rate, CPU, thermal, and determinism gates part of release qualification.
- Keep protocol v1 available as a test oracle and temporary rollback option until protocol v2 is qualified.

**Non-Goals:**

- Prediction, rollback, run-ahead, reconnect, host migration, spectators, relay service, or internet-latency support.
- More than two players, Single-Pak multiboot, RFU, or non-MULTI serial networking.
- Different-ROM compatibility groups in the first replicated release.
- A RetroArch fork, direct sockets, a bespoke Android application, or reliance on frontend savestate rollback.
- Persisting a remote shadow player's save on the local device.
- Hiding a peer loss by continuing an independently diverged local cable session.

## Decisions

### 1. Replace distributed SIO with a replicated local link pair

During a protocol-v2 session, each physical endpoint owns two `mCore` instances connected to one local `GBASIOLockstepCoordinator`:

```text
host device                              client device

local core P0  <--- local SIO ---> P1'   replica P0' <--- local SIO ---> local core P1
display/audio/input/save: P0             display/audio/input/save: P1
shadow input/save: P1                    shadow input/save: P0
```

Both endpoints load identical snapshots for logical players P0 and P1, attach them in deterministic player-ID order, apply the same frame inputs, and run the same two-core schedule. SIO mode changes and serial words never cross the network. They are ordinary in-process lockstep events and therefore retain mGBA's existing timing, register, IRQ, attach, and detach behavior.

This is chosen over optimizing protocol-v1 messages because even one round trip per dependent word is far below cable rate. It is chosen over game-specific batching because arbitrary software can branch between words. It is chosen over a RetroArch fork because the core already controls both emulated machines and Netpacket provides the needed reliable session transport.

### 2. Prove the two-core runtime in a bounded feasibility spike

Before changing the production protocol, a disposable libretro test path SHALL create two GBA cores from the same ROM, give them different deterministic inputs, attach two `GBASIOLockstepDriver` instances, and run the continuous-transfer test ROM for at least ten emulated seconds.

The spike compares two scheduling candidates:

1. two worker contexts using mGBA's existing thread lockstep user; and
2. a cooperative single-frontend-thread scheduler whose sleep/wake callbacks yield between cores at timing boundaries.

The selected scheduler must keep all libretro callbacks on the frontend thread, stop both cores without deadlock, and reproduce identical trace hashes across repeated normal and sanitizer runs. It must then run on both Android devices while recording real emulation FPS, serial words per emulated second, wall CPU, memory, device temperature, and teardown latency.

The production migration is blocked if the exact devices cannot run two local GBA cores at at least 59 emulated frames per second without sustained thermal throttling. In that case the fallback investigation is a same-device authoritative cable service with speculative batches and rollback, not incremental per-word Wi-Fi transactions.

### 3. Establish replicas from bilateral quiescent snapshots

The attachment handshake remains bilateral and exact-ROM-only, but protocol v2 adds a `REPLICA_MANIFEST` and chunked snapshot exchange after identity acceptance.

Each peer pauses its existing local core at a quiescent attachment boundary and produces a canonical replica bundle for its assigned logical player. The bundle contains all mutable machine state needed to reproduce execution, explicit save-memory bytes and type, RTC state required by emulation, logical frame/cycle metadata, and a format version. It excludes ROM bytes, filenames, paths, frontend configuration, audio/video buffers, Netpacket state, and existing network-driver state. The effective ROM is already present and identity-validated on both devices.

```text
HELLO / ACCEPT / ACCEPT_ACK
    -> REPLICA_MANIFEST(player, sizes, chunk size, digests)
    -> REPLICA_CHUNK(player, offset, bytes) ...
    -> REPLICA_INSTALLED(player, digest)
    -> SESSION_READY(input delay, attach generation)
    -> SESSION_READY_ACK
```

Reliable packets are still decoded and copied into bounded generation-scoped queues. Chunks are capped below the frontend packet-size limit. The receiver rejects overlap, holes, inconsistent duplicates, decompression expansion beyond the manifest, digest mismatch, unsupported state format, or a bundle for the wrong player. No bundle is installed until it is complete and verified.

After both bundles are known, each endpoint constructs fresh P0 and P1 cores, loads the same corresponding bundle into each logical slot, creates a fresh coordinator, and attaches drivers in P0-then-P1 order. The old single core remains intact until the replacement pair is fully built so attachment failure can return to single-player without mutating the user's running state.

This design permits different player saves and different menu progress at connection time while making the resulting pair identical on both endpoints.

### 4. Synchronize delayed frame inputs, not cable events

The runtime uses fixed-delay deterministic input synchronization. Session acceptance selects an input delay `D` in whole GBA frames from both peers' supported range and measured handshake RTT/jitter, clamped to a conservative MVP range. There is no prediction.

At replicated frame `F`, each endpoint samples its assigned physical controller for logical frame `F + D` and sends one reliable flushed `INPUT_BATCH` containing that frame plus a short redundancy window of recent local inputs. Each endpoint may execute frame `F` only when authoritative inputs for both P0 and P1 at `F` are installed. An exact duplicate is idempotent; conflicting input for an installed frame fails the session.

Input ownership is fixed by the accepted assignment: host packets can author P0 only and client packets can author P1 only. The local value is applied to the local-role core and the received value to the shadow-role core. Both cores then run to their persistent next video-frame targets under the local lockstep scheduler.

One packet per player per frame keeps ordinary wire traffic O(video frames), independent of the number of SIO transfers. The short redundancy window is not required for reliability but makes trace diagnosis and duplicate behavior explicit.

### 5. Wait inside a bounded `retro_run()` rendezvous

The libretro adapter SHALL not repeatedly return empty blocked calls while a required input is expected within its operation deadline. `retro_run()` polls receive through the generation-safe wrapper and processes the copied queue until the current frame is runnable, the deadline expires, or stop invalidates the generation. Once runnable, it advances exactly one replicated pair frame and supplies the local-role core's newly produced video and audio.

This same bounded in-call rendezvous is first implemented against protocol v1 frame grants as a tactical regression fixture. That proves the audio-starvation diagnosis and provides a small safe improvement for the retained fallback. It is not considered a solution to active commercial-game throughput.

Polling remains bounded and handles synchronous `receive` and `stop` re-entry. If polling is unavailable, the replicated runtime is not advertised as supported. A missed input deadline tears down the session; it does not emit silence indefinitely, guess input, or run either replica alone.

### 6. Use a deterministic pair scheduler and persistent per-core frame targets

The pair owns a monotonically increasing replicated frame number. A frame begins only after both inputs exist. The scheduler sets both cores' keys, advances P0 and P1 until each reaches its persistent next video-frame target, and lets the local coordinator arbitrate any SIO sleeps and wakes between them. The targets are initialized from each restored core's own frame counter; the authoritative replicas are not required to have been captured at equal raw video counters.

Servicing an outstanding local cable dependency can require a logical core that has reached its target to cross one additional video boundary before its peer reaches its own target. The scheduler accepts that bounded lead only after both targets have been reached, records it in diagnostics, and rebases only the affected core's next target from the observed counter. Advancing more than one boundary beyond a target fails closed. The replicated pair frame still advances exactly once, and identical endpoint pairs make the same recovery and produce the same state traces. This exception is explicit because treating any lead as a fatal overshoot caused a deterministic commercial-game failure, while stopping the leading core would deadlock the in-process cable.

The selected spike scheduler becomes a small transport-independent `GBAReplicatedPair` module. It owns both cores, the coordinator, two lockstep drivers/users, frame state, and deterministic construction/destruction. Netpacket, input queues, and libretro callbacks remain outside this module. Unit tests can therefore drive the pair with scripted inputs and compare its logical-player states without RetroArch.

Only the assigned local-role core's pixels, audio buffer, rumble, sensors, and camera/solar callbacks are frontend-visible. Shadow output is drained or discarded deterministically. Frontend input is sampled once and never read by the shadow core.

### 7. Make save ownership explicit and transactional

P0 is owned by the host device and P1 by the client device. During the session each endpoint holds identical volatile copies of both players' save memory, but only the local-role core is eligible to update the frontend-visible `RETRO_MEMORY_SAVE_RAM` and the normal save file.

The shadow save is memory-backed and never receives the user's normal save path. At clean teardown, reset, timeout, or transport failure, the pair first stops at a deterministic safe boundary when possible, copies the local-role save back to the original single-core/save backing, and destroys the shadow. A digest and dirty-generation counter prevent an older shadow or pre-session snapshot from overwriting newer local save data. If safe extraction cannot be proved, teardown preserves the last valid local save and reports that in diagnostics rather than writing uncertain data.

State save/load remains rejected in every non-disconnected state. Protocol snapshots are internal attachment data and are not exposed as user savestates.

### 8. Detect replica divergence independently from transport health

Every configurable checksum interval, initially 60 frames, each endpoint computes canonical hashes for P0 and P1 over emulated state that can affect future execution. The hash definition is versioned and excludes presentation buffers, host pointers, caches, logging counters, wall clock, transport state, and save-path metadata.

Peers exchange `STATE_CHECK(frame, p0_digest, p1_digest)`. A mismatch at the same frame fails closed before another input window is released and logs the first mismatching logical player, frame, local/remote digest, recent input window, session ID, protocol version, and non-sensitive runtime policy. Periodic checks diagnose divergence; they do not repair it.

Automated tests compare full deterministic trace hashes every frame even when the production wire check is less frequent.

### 9. Version the protocol and keep v1 as a temporary oracle

Protocol v2 has independent packet, snapshot-chunk, input-frame, and checksum sequence domains. It removes `EXECUTION_GRANT`, `MODE_*`, `TRANSFER_*`, and `COMPLETION_*` from the normal replicated runtime. Those packet types remain understood only by the explicit protocol-v1 fallback build/option during migration.

Protocol version mismatch fails during `HELLO`; there is no automatic downgrade because that could silently select the known-unplayable architecture. Traces record the selected runtime (`distributed-sio-v1` or `replicated-pair-v2`). Release builds select v2 once its qualification gates pass; v1 remains available to tests as an oracle for short deterministic SIO traces and failure semantics until the new tests cover equivalent behavior.

### 10. Treat performance and observability as correctness gates

The adapter records structured counters without logging every packet by default:

- real displayed frames, emulated frames, blocked-rendezvous duration, input-buffer depth and deadline misses;
- packets/bytes by type, RTT/jitter samples, duplicate/conflict counts and queue high-water marks;
- local SIO transfers/words and local-lockstep waits;
- audio frames produced per displayed frame and zero-audio returns;
- per-core run time, process CPU, memory, thermal samples where Android exposes them;
- state-check frame and result.

A diagnostic summary is emitted at attach, every configurable reporting interval, and teardown. Verbose packet traces remain opt-in and sampled.

The release gate on the exact Android build and device pair is:

- at least 59 emulated/displayed FPS during idle connection and continuous MULTI traffic;
- no recurring empty-frame audio starvation or Android `AudioTrack` stop storm attributable to the core;
- serial throughput within 5% of the same build's two-core localhost lockstep baseline;
- ordinary network traffic scaling with frames rather than serial words, with no v1 transfer packets;
- no checksum mismatch in a 30-minute continuous test-ROM run;
- successful fresh-connect, multiplayer entry, gameplay, and clean detach in at least one selected commercial Multi-Pak title;
- an explicit compatibility result for every attempted qualification title, including Four Swords, without treating real-time discovery dwell as successful linking;
- recorded input delay, p95 rendezvous duration, CPU, peak memory, temperature, and absence of sustained thermal throttling.

## Risks / Trade-offs

- **Two cores exceed mobile CPU or thermal budget** → Gate production work on the Android spike, disable shadow rendering/audio work, profile both scheduler candidates, and stop rather than publishing a build that misses real time.
- **Existing local lockstep assumes independently running threads** → Exercise teardown, reset, sleep/wake, and continuous transfers under sanitizers; isolate any cooperative/thread adaptation in `GBAReplicatedPair` rather than changing cable semantics.
- **Internal snapshots omit mutable cartridge or RTC state** → Define and test an explicit bundle rather than assuming libretro serialization alone is sufficient; round-trip every supported save type.
- **Different peer attachment moments produce inconsistent replicas** → Each logical player has exactly one authoritative source bundle, and both endpoints install the same digests before attachment.
- **Fixed input delay adds latency** → Select the smallest delay justified by measured jitter, expose it in diagnostics, and prefer predictable latency over rollback complexity for the MVP.
- **A stalled peer blocks `retro_run()`** → Bound the in-call poll by operation deadline, honor synchronous stop invalidation, and teardown with an actionable input-frame diagnostic.
- **Replica divergence corrupts saves** → Fail closed, retain the last known-valid local save generation, and never persist shadow-owned data.
- **Protocol v1 and v2 complicate maintenance** → Keep v1 behind an explicit diagnostic selection and remove it after v2 has equivalent tests and two release cycles of qualification evidence.
- **Memory use doubles or more during attachment** → Stream bounded chunks, cap manifest sizes, and keep the original core only until the replacement pair is verified.

## Migration Plan

1. Preserve the approved protocol-v1 branch and captured Odin/Thor evidence as the baseline.
2. Add the bounded in-call v1 grant rendezvous plus an automated audio/frame-pacing regression.
3. Implement and measure the transport-independent two-core feasibility spike on Linux and both Android devices.
4. If the spike passes, add the replica bundle codec and pair construction tests without changing the default runtime.
5. Add protocol-v2 snapshot, delayed-input, checksum, save-ownership, and teardown state machines behind an experimental core option.
6. Run deterministic replay, fault injection, normal CI, ASan/UBSan, fixture ROM, and save-type round-trip suites.
7. Install the exact build on both devices, qualify the test ROM and at least one selected commercial title against the performance gates, and record Four Swords as a non-blocking named compatibility investigation for this experimental alpha.
8. Make v2 the default only after the evidence is committed; retain an explicit v1 diagnostic option for rollback.
9. Remove v1 runtime code in a later change after equivalent coverage and field confidence exist.

Rollback before step 8 is selecting the unchanged v1 runtime. Rollback after step 8 is one core option/build change; protocol negotiation never mixes the two execution models in one session.

## Open Questions

- Which scheduler candidate meets the Android real-time and teardown gates with the smallest upstream-facing change?
- What is the smallest canonical replica bundle that round-trips every supported GBA save type and RTC mode without embedding frontend state?
- What fixed input-delay bounds are justified by the measured Odin/Thor Wi-Fi jitter, and should the initial choice be immutable for the session?
- Which canonical core-state fields provide a stable, inexpensive divergence hash across supported Android ABIs?
- Can shadow video rendering be disabled safely, or must it render into a small private buffer to preserve timing behavior?

These questions are answered by the ordered spike and characterization tasks; none requires returning to a per-word network design.
