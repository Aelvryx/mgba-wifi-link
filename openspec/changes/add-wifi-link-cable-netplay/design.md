## Context

The project currently contains OpenSpec planning files but no Git repository or mGBA source tree. Implementation must establish the project as a real fork whose history descends from a pinned upstream mGBA commit; a source snapshot import would discard useful ancestry before central SIO and timing work begins.

Current mGBA already separates GBA serial hardware emulation from SIO drivers. Its local `GBASIOLockstepDriver` and coordinator model player assignment, timestamped mode changes, a shared cable clock, transfer start and completion, returned MULTI words, hard synchronization, and attach/detach behavior. The network change should preserve those hardware semantics without serializing the in-process coordinator or adding game-specific protocols.

Two details of the current common SIO implementation affect this design:

- `GBASIOWriteSIOCNT()` consults `handlesMode()` for some register handling, but `_startTransfer()` calls an attached driver's `start()` and `connectedDevices()` hooks without first verifying that the driver handles the active mode.
- Normal `_sioFinish()` and `GBASIOMultiplayerFinishTransfer()` code installs receive words, clears MULTI busy, restores line state and device ID, and raises the serial IRQ when enabled. Removing a scheduled completion event does not perform those transitions.

The current libretro Netpacket ABI uses `RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE` command 78. It provides a protocol-version string, reliable ordered sends, a flush hint, receive polling for bounded mid-frame waits, and start/receive/stop/poll/connect/disconnect callbacks. A `poll_receive_fn` call can synchronously invoke receive and stop callbacks; after stop, saved frontend function pointers are invalid. mGBA's vendored `libretro.h` predates this ABI and must be refreshed from a pinned canonical revision.

Stock RetroArch owns discovery, connection setup, Android networking, and transport. The core owns its versioned cable protocol and emulated synchronization. Netpacket calls remain on the libretro execution thread and no direct sockets are introduced.

## Goals / Non-Goals

**Goals:**

- Provide generic, deterministic two-device GBA Multi-Pak play over a same-LAN Netpacket session.
- Preserve two-way causal safety for every cable-visible client action supported by the MVP.
- Lead virtual execution from player zero so no valid host-originated event can arrive in player one's emulated past.
- Reuse common mGBA transfer scheduling and completion behavior while keeping network coordination separate from local lockstep.
- Make player zero authoritative for topology, compatibility policy, cable event commits, transfer sequence allocation, timing, and results.
- Make quiescent session attachment and mode readiness observationally atomic across both peers.
- Fail through an explicit, testable SIO register transition rather than leaving a transfer permanently busy.
- Preserve one authoritative completion cycle for every success or recoverable failure after transfer start emission.
- Freeze timing-sensitive configuration and time-changing operations throughout every non-disconnected session state.
- Keep single-player, unsupported serial modes, and local same-process link behavior unchanged.
- Validate Netpacket and Android lifecycle assumptions before building the production protocol.

**Non-Goals:**

- Three- or four-player sessions.
- Single-Pak multiboot.
- RFU/Wireless Adapter emulation.
- NORMAL8, NORMAL32, UART, GPIO, or Joy Bus networking.
- Rollback, prediction, reconnection, host migration, relay service, NAT traversal, or a custom socket stack.
- A RetroArch fork, bespoke Android frontend, or game-specific serial emulation.
- Savestate synchronization or loading states during an active session.
- Guaranteeing smooth real-time play on high-latency internet links.
- A compatibility-group database in the MVP; the wire model only reserves the policy.

## Decisions

### 1. Preserve upstream history and pin both external revisions

The repository will be initialized with `upstream` pointing to `mgba-emu/mgba`, the chosen upstream commit will be fetched, and the feature branch will be created from that commit. The existing planning files will then be committed on top of the upstream ancestry. If they have already been committed elsewhere by implementation time, that commit will be cherry-picked instead of importing a source snapshot.

`UPSTREAM.md` will record the mGBA URL, commit, branch, baseline build/tests, canonical libretro header URL and revision, and any local patch-stack conventions. This makes blame, rebasing, comparison, and eventual upstreaming practical.

### 2. Run a disposable Netpacket feasibility spike before production work

After the baseline builds and the canonical libretro header is refreshed, a small spike will register command 78 from `retro_load_game`, exchange ping/ack packets between two stock RetroArch instances, call `poll_receive_fn` from an mGBA timing boundary, and record synchronous receive and stop re-entry.

The spike runs first on Linux localhost and then on two Android devices over Wi-Fi. It records frontend/core versions, RTT distribution, callback cadence, longest wait, stop behavior, custom-core installation steps, and whether all required callbacks are available. Production timing defaults and supported frontend versions are not selected until this evidence exists. The spike code is removed or isolated from production modules once its findings are recorded.

This front-loads the only external integration on which the architecture depends.

### 3. Add a separate network driver and make common SIO dispatch mode-safe

`GBASIONetplayDriver` remains beside `GBASIOLockstepDriver`; Netpacket calls are not added to `lockstep.c`.

```text
libretro Netpacket adapter
  - callback lifecycle, copied queues, transport generation, messages
                  |
transport-neutral protocol/session
  - codec, handshake, policies, counters, deadlines
                  |
GBASIONetplayDriver
  - committed mode state, cable-time mapping, grants and transfers
                  |
common GBA SIO
  - mode-gated hook dispatch and existing completion path
```

The GBA layer receives a transport vtable for reliable send, flush, receive poll, diagnostics, monotonic time, and stop; it does not include `libretro.h`.

The common SIO layer gains one consistent predicate equivalent to “driver exists and handles the relevant mode.” For new I/O, the relevant mode is the current mode; it gates driver-specific `start`, `connectedDevices`, `deviceId`, `writeSIOCNT`, `writeRCNT`, and mode-specific register hooks. When common code schedules a completion event, it latches that transfer's mode. Finish dispatch uses the latched completion mode, not a later `sio->mode`, and invokes the corresponding finish hook only if the driver handles the latched mode. This prevents a mode write after START from rerouting an already scheduled MULTI completion into NORMAL or dummy completion. `setMode` remains a lifecycle notification so the network driver can detect both entry to and departure from MULTI, but otherwise unsupported-mode I/O follows exactly the existing no-driver path.

Common start dispatch also gains an explicit result carrying whether common code schedules completion and the effective peer count for that transfer. Existing drivers receive adapters preserving their current boolean/`connectedDevices()` behavior; the network driver supplies the value directly:

```text
GBASIOStartResult
  completion_owner = COMMON | DRIVER
  effective_transfer_peer_count = 0 | 1
```

`topological_peer_count` remains session bookkeeping and equals one after acknowledged attachment. It is never inferred from `connectedDevices()` and never selects transfer duration. `GBASIONetplayDriver.connectedDevices()` reports the idle effective cable count—zero until joint MULTI readiness commits and one while jointly ready—for line-state queries. For a start, the explicit result snapshots the scheduling count: zero before readiness or before START emission, and one after START emission—including `abort_pending`—so common scheduling deterministically selects the announced two-device duration even if readiness changes later. There is no temporary undocumented mutation of `connectedDevices()` inside `start()`.

This is an upstream-quality common fix with direct regression tests, not a network-driver workaround.

### 4. Use an atomic bilateral session handshake

Frontend client ID zero is player zero and host. The host accepts at most one remote frontend client and assigns it player one. A local transport-generation token increments on every Netpacket start/stop and invalidates queued work from earlier transports.

On Netpacket start, each core enters a handshake rendezvous at its next quiescent SIO boundary. Quiescent means no active or scheduled SIO completion, MULTI busy clear, and no reset or unload transition. A pending ordinary no-driver/no-peer transfer is allowed to finish before the rendezvous; failure to become quiescent expires the attachment deadline. The core pauses at that boundary, snapshots its current local SIO mode and local cycle, and only then sends `HELLO`. This prevents an old completion from reaching a newly installed driver and ensures a game already in MULTI does not depend on a future `setMode` callback.

`HELLO` and any rejection before acceptance use header `session_id = 0`. `ACCEPT` also uses header session ID zero and carries the proposed nonzero session ID in its payload; the client installs that ID before sending `ACCEPT_ACK`. Every subsequent packet uses the nonzero session ID. Both peers send `HELLO`, allowing each to learn the other's content identity, determinism categories, and initial mode snapshot. The host does not accept until it has both identities and quiescent snapshots.

```text
host and client: pause at local quiescent SIO rendezvous
host and client: HELLO(protocol, capabilities, ROM identity,
                       supported policies, compatibility version,
                       determinism categories, initial mode snapshot)
host:            ACCEPT(nonzero session_id, assignments, EXACT_ROM,
                        compatibility_group=0, attach_cycle)
client:          ACCEPT_ACK(session_id)
host:            SESSION_READY(session_id, attach_cycle,
                               initial mode generation)
client:          SESSION_READY_ACK(session_id)
host:            install final acknowledgement, attach internally,
                 and send the first initial-mode commit or execution grant
client:          attach internally but remain paused until that first
                 valid post-attachment host message
```

Player zero does not pass the attachment cycle before receiving `SESSION_READY_ACK`. After sending that acknowledgement, player one may install pending state internally but does not execute GBA software that can observe attachment until it receives the first valid post-attachment initial-mode commit or execution grant. Both mode snapshots form the initial mode generation, which is committed before ordinary grants begin. A mode write cannot race the barrier because both cores remain paused after their quiescent snapshot.

Handshake initiation is a user/frontend lifecycle event, so determinism is scoped from each core's quiescent rendezvous onward; packet latency before the rendezvous is not represented as an emulated cable event. An exact duplicate pre-session message is idempotent and may cause the corresponding response to be resent. A conflicting duplicate, invalid transition, or packet from an unexpected peer rejects the handshake. Stop at any intermediate state clears queues and mappings and leaves the cable detached.

The state machine is:

```text
DISCONNECTED -> TRANSPORT_STARTED -> HELLO_EXCHANGED -> ACCEPTED
             -> ATTACH_BARRIER -> READY -> TRANSFERRING -> READY
             any failure -> FAILED -> DISCONNECTED
```

### 5. Exchange identity separately from compatibility policy

Every `HELLO` carries SHA-1 and byte length over the effective loaded GBA ROM after frontend extraction/patching and before mutable emulation state. SHA-1 is a stable identity, not a security primitive. No ROM bytes, save data, filenames, paths, or secrets are sent.

The handshake represents compatibility as:

- `EXACT_ROM`: both identities must match; this is the only MVP policy and the default.
- `COMPATIBILITY_GROUP`: reserved for a future version in which known ROM identities can share a link-protocol group. The packet fields and policy enum exist, but accepting this policy is not implemented in the MVP.

This preserves strict first-release validation without making exact equality a permanent property of a generic cable architecture.

### 6. Validate a timing-sensitive determinism profile

Exact ROM identity is necessary but insufficient for repeatable emulation. The wire carries a stable `emulation_compatibility_version` plus a versioned list of per-category digests for settings that can change execution or timing:

- mGBA timing-model/profile compatibility, represented by the explicit emulation-compatibility version rather than compiler, Android ABI, commit string, or other harmless build metadata;
- BIOS versus HLE selection and BIOS identity when used;
- idle optimization, CPU/timing, overclock, and speed-hack options;
- RTC override mode, excluding the independent runtime RTC value;
- active-cheat state, with cheats required to be disabled for the MVP;
- future options explicitly classified as timing-sensitive.

Visual, audio-filter, controller-mapping, independent save-memory contents, inputs, and the runtime RTC value are excluded. The canonical inputs and per-category encoding are protocol-defined; raw serialized configuration and arbitrary build metadata are never hashed. Peers compare each named category, so diagnostics can identify a mismatching category without putting configuration values on the wire.

From `TRANSPORT_STARTED` through `FAILED` teardown, a timing-sensitive core-variable update or cheat API call is rejected and does not take effect. The core reports that link netplay must be disconnected first. Settings outside the profile remain changeable according to existing behavior.

### 7. Use explicit codecs and independent sequence domains

Packets use a fixed little-endian codec, never packed C structs. The common header contains magic, exact protocol version, type, payload length, session ID, and a per-sender 64-bit packet sequence.

Independent 64-bit counters exist for:

- per-sender packets;
- session generation/ID;
- execution grants;
- mode generations/barriers;
- transfers;
- transfer-completion barriers;
- optional health barriers.

Payloads carry their relevant domain counter; “message or transfer sequence” is never overloaded. Counters never wrap within a session: reaching the maximum triggers a clean detach and requires a new session. Duplicate rules are defined per message type, and invalid skipped/future references fail before emulated state changes.

Session messages are `HELLO`, `ACCEPT`, `ACCEPT_ACK`, `SESSION_READY`, `SESSION_READY_ACK`, `REJECT`, `DETACH`, and `DETACH_ACK`.

Runtime messages are `EXECUTION_GRANT`, `GRANT_ACK`, `MODE_INTENT`, `MODE_COMMIT`, `MODE_ACK`, `TRANSFER_START`, `TRANSFER_READY`, `TRANSFER_COMMIT`, `TRANSFER_ABORT`, `COMPLETION_CATCHUP`, `COMPLETION_READY`, `COMPLETION_DECISION`, and `COMPLETION_DECISION_ACK`. `TRANSFER_ABORT` carries the transfer sequence, immutable authoritative completion cycle, and a stable reason code. Completion messages carry both transfer and completion-barrier sequences; the decision carries the authoritative success/error outcome and the final acknowledgement confirms that player one installed that decision. All use reliable ordered delivery; latency-sensitive messages use the flush hint.

### 8. Separate local scheduling, network grants, and hard barriers

Three timing concepts remain independent:

1. **Local scheduler quantum:** a fine-grained `mTiming` check for local queues. It sends no packet and can use values appropriate to in-process emulation.
2. **Network execution-grant cadence:** host-issued virtual cable horizons, initially frame-oriented and strictly single-flight. The initial policy is approximately one candidate epoch per emulated video frame, selected after the feasibility spike.
3. **Hard barriers:** atomic attachment, committed mode transition, transfer start, and transfer completion. No periodic hard barrier is enabled by default. A much less frequent health barrier may be introduced only from measurement.

These values are runtime policy, visible in trace output and configurable in the test harness. They do not contribute to wire-protocol compatibility.

Wire timestamps use a monotonic 64-bit virtual cable clock with an explicit mapping to each local `mTiming` domain. Execution is host-led:

1. From the last acknowledged boundary, player zero executes toward a candidate horizon, stopping immediately at any host-originated cable event.
2. Only after player zero has reached a horizon `H` may it send `EXECUTION_GRANT(H)`. Player one never holds a grant beyond player zero's current cable cycle.
3. At most one execution grant is outstanding. Player zero remains paused at `H` until player one either acknowledges safe progress through `H` or reports a mode intent at an earlier local boundary.
4. If player zero encounters a transfer start at `T` before the candidate horizon, no grant past `T` exists. `TRANSFER_START` becomes a catch-up barrier at `T`.
5. At that barrier, player one either reaches `T` and sends `TRANSFER_READY`, or stops at an earlier mode intent `C < T`; the mode generation resolves before the transfer can continue.

Consequently, a valid `TRANSFER_START` cannot arrive in player one's past. Receiving one in the past remains a fail-closed invariant check, not an expected latency outcome. Grant cadence is a runtime performance policy; single-flight host-leading order is a protocol invariant.

When any of these boundaries puts a player to sleep from inside an `mTiming`
callback, the driver interrupts the active GBA timing pass before returning.
This is the same wake/sleep pattern used by local lockstep and prevents later
events already queued in that pass—especially a remote SIO completion—from
running after the new pause became effective.

### 9. Commit mode readiness through a barrier

A local SIO mode write changes that GBA's local mode immediately, but it does not independently change shared cable readiness.

Outside an active transfer, when either peer enters or leaves MULTI:

1. The driver records `MODE_INTENT(mode_generation, local_mode, local_cycle)` and pauses that core at the SIO boundary.
2. The host stops no later than the current execution-grant boundary and validates the intent against the committed session/mode generation.
3. The host chooses the deterministic, unpassed grant boundary as `MODE_COMMIT` cable time.
4. Both peers install the peer-visible mode state at that barrier and exchange `MODE_ACK`. A peer paused before the boundary rebases only its local-to-cable cycle offset so its unchanged local `mTiming` cycle maps to the committed cable cycle; no emulated CPU cycle is executed, skipped, or rewound by the wall-clock wait.
5. After sending `MODE_ACK`, player one may install pending state internally but remains paused and cannot execute an instruction that observes it until player zero receives both acknowledgements and releases the next grant.

The attachment snapshots are submitted as the initial mode intent generation, including when a game entered MULTI before driver installation. Until a generation commits, the cable is not exposed as jointly ready even though the originating GBA has changed its own local mode. A transfer-start barrier that encounters an earlier uncommitted mode intent resolves the mode transition first and uses the ordinary no-peer path if the committed modes are no longer both MULTI.

A mode write after START emission uses a separate non-blocking path because the immutable transfer must first reach completion:

1. The driver records a deferred mode intent with its local cycle and changes local mode normally, but does not enter the ordinary blocking mode barrier.
2. It marks the transfer `abort_pending` and sends the deferred intent plus `TRANSFER_ABORT` when transport remains usable.
3. The host continues its host-led run to the announced completion cycle. A client that encounters the write while later catching up to completion also continues to that cycle; neither role pauses at the mode-write boundary.
4. If the host is already waiting at completion when the client discovers the write, the client's `COMPLETION_READY` reports the abort and deferred generation.
5. The authoritative completion decision selects the error outcome. After both healthy peers complete, the host commits all deferred mode intents in deterministic cycle/sender order at the completion boundary before issuing another ordinary grant. A terminally failed session detaches instead.

This active-transfer exception removes the circular dependency in which a mode barrier waits for completion while the mode-writing peer waits at an earlier cycle.

This explicit barrier is chosen over a fully symmetric zero-lookahead conservative simulation, which would require excessive traffic for arbitrary SIO writes.

### 10. Use the common completion path for successful transfers

Player zero is the only transfer initiator.

1. In the player's `GBASIODriver.start()` hook, the host validates atomic attachment and committed MULTI readiness, allocates the transfer sequence, captures its word/SIOCNT, computes the completion cycle with `GBASIOTransferCycles(GBA_SIO_MULTI, siocnt, 1)`, sends `TRANSFER_START`, and blocks at the current emulated cycle. Because player zero reached the start cycle before granting beyond it, player one cannot already have passed this boundary.
2. Player one processes remote start at the mapped cycle, sets MULTI busy, captures `SIOMLT_SEND`, schedules its existing `sio->completeEvent` for the announced completion cycle as local lockstep does, and sends `TRANSFER_READY`.
3. The host validates readiness and sends `[p0, p1, 0xFFFF, 0xFFFF]` in `TRANSFER_COMMIT`. This is a candidate successful result, not an execution grant and not the outcome commit point; player one remains paused at start cycle `T`.
4. The host returns `GBASIOStartResult(COMMON, 1)`. Common mGBA `_startTransfer()` uses that explicit effective count to schedule the host's existing completion event for `C`. The network driver does not manually clear host busy or raise its IRQ.
5. Player zero executes first from `T` to `C`. On entering its `finishMultiplayer` hook, it sends `COMPLETION_CATCHUP(transfer_sequence, completion_sequence, C, pending_outcome)` and remains paused at `C`.
6. Receipt of valid `COMPLETION_CATCHUP` is the client's sole authorization to execute from `T` to `C` while transport remains healthy. Player one reaches its scheduled completion hook, sends `COMPLETION_READY` with its local abort/deferred-mode status, and remains paused at `C`.
7. Player zero validates readiness, chooses the authoritative success or error result, and sends `COMPLETION_DECISION`. This decision is the outcome commit point. Player one validates and installs it, sends `COMPLETION_DECISION_ACK`, and then returns its finish hook.
8. Player zero remains in its finish hook until it receives the matching acknowledgement or the terminal delivery deadline/failure resolves. A lost final acknowledgement closes the session but does not rewrite an already committed host outcome.
9. Each returning hook supplies the decided words to common `GBASIOMultiplayerFinishTransfer()`, which installs data, clears busy, restores line/ID state, and raises each local IRQ once when enabled.

Selecting driver-owned host completion is rejected because it duplicates common completion semantics. The remote client path necessarily schedules the existing completion event because its transfer starts from a network event rather than a local start write.

Every MULTI start path is classified before it changes state:

| Condition | Required outcome |
| --- | --- |
| Player zero starts while detached or before committed joint MULTI readiness | No network sequence or START; the characterized ordinary no-driver/no-peer start path runs with `effective_transfer_peer_count = 0`. |
| An earlier mode barrier resolves to not-both-MULTI while host `start()` is waiting | No START; `start()` releases to the same ordinary no-peer path with effective count zero. |
| Player zero starts while ready | Successful network completion, post-START erroneous completion, or reset/unload cancellation. |
| Player one independently writes start | Existing non-initiating secondary wait behavior: busy remains set while waiting for a primary clock, but there is no network sequence, remote start, completion event, or IRQ created by the network driver, and committed slave line/ID state is retained. |
| Either peer leaves MULTI after START emission | The intent is deferred without blocking progress, the transfer error-completes at its announced completion cycle, and the mode generation commits before the next grant. |
| A scheduled remote START and CPU mode write share cycle `T` | The already-scheduled START event runs first; the CPU write is a post-START deferred intent and makes the outcome erroneous at `C`. |
| A scheduled completion and CPU mode write share a virtual cycle | Existing `mTiming` ordering is frozen by test: the already-due completion runs before the subsequent register write creates a new mode intent. |

These are exhaustive. A path may end only in successful network completion, characterized erroneous completion, ordinary no-driver/no-peer behavior, or reset/unload cancellation.

The active transfer retains its scheduled MULTI completion context even if a later register write changes the current local SIO mode. Common finish dispatch uses that latched mode and the error helper updates only the characterized transfer result/line fields while preserving the later mode selection. This is a narrow common-SIO correctness change with regression tests for every existing driver, not permission for unsupported-mode network hooks.

### 11. Define a complete error-completion transition

`GBASIONetplayAbortTransfer()` represents a recoverable cable failure while emulation continues. It does not merely cancel `completeEvent`.

The point of no return is successful `TRANSFER_START` emission into the reliable transport path, not readiness or commit:

- Before START emission, no network transfer exists. Failure releases the host to the characterized disconnected/no-peer start path.
- After START emission, the announced completion cycle is immutable. Success and every recoverable failure complete at that cycle.
- If readiness is lost, commit cannot be sent, or a later protocol/transport error occurs, the host marks `abort_pending`, returns `GBASIOStartResult(COMMON, 1)` after emitted START so common scheduling uses the announced two-device duration, and supplies the error result from `finishMultiplayer`.
- A peer that can still send transmits `TRANSFER_ABORT(transfer_sequence, authoritative_completion_cycle, stable_reason_code)`. When transport is dead, each side that accepted START uses its retained cycle locally.
- Failure after `TRANSFER_COMMIT` replaces the successful words with the error result without changing the completion cycle.
- If START was accepted by one endpoint but never delivered to the other before transport death, the accepting endpoint error-completes at the retained cycle; the endpoint that never accepted START has no transfer to complete and performs idle detach cleanup.
- If the completion boundary is reached while waiting, emulated execution remains paused there until the success/error result is resolved under its deadline; no later wall-clock instant becomes the emulated completion cycle.
- If transport terminates after the client accepted START but before `COMPLETION_CATCHUP`, the healthy-protocol grant invariant no longer applies: the client locally advances only to retained cycle `C` to perform the required error completion.

While the transport generation remains healthy through delivery of `COMPLETION_DECISION`, both peers complete at `C` with the same authoritative outcome. `COMPLETION_DECISION_ACK` lets the host confirm that player one installed the decision, but bounded distributed commit still cannot guarantee identical observations if the generation terminates during final decision delivery:

- If the decision send is rejected before the host commits it, the host selects local error at `C`; the client also follows terminal post-START error handling.
- Once the active transport accepts the decision, the host outcome is committed but its hook remains blocked for the acknowledgement. If transport then dies before player one receives the decision, the host returns that committed outcome while player one error-completes locally at `C` after stop/deadline. A successful host outcome and erroneous client outcome are therefore an explicitly permitted asymmetric terminal observation.
- If player one receives a valid decision before termination, it returns that decision and both outcomes match. If only its final acknowledgement is lost, the host preserves the same committed outcome but fails the session so no subsequent transfer can begin.

The acknowledgement narrows diagnostics and release ordering; it does not claim impossible atomic delivery and merely makes the acknowledgement itself the final-message failure window.

The error finish hook and common completion path produce this deterministic state:

- `SIOMULTI0..3 = 0xFFFF`;
- SIOCNT busy/start is cleared;
- SIOCNT communication error is set;
- SIOCNT ready is one and slave is one, reflecting pulled-up disconnected lines;
- device ID becomes zero/no assigned slave;
- RCNT SC is high/idle;
- exactly one SIO IRQ is raised if the local IRQ-enable bit is set;
- the session topology detaches, the effective cable/transfer peer count becomes zero, and subsequent writes follow no-peer behavior.

This models a completed erroneous transfer with invalid pulled-up data, rather than fabricated peer data or an eternal busy wait. The frozen evidence, register tables, timing values, and evidence limits are recorded in `docs/gba-sio-characterization.md` and enforced by `src/gba/test/sio.c`.

The ordinary pre-START no-driver/no-peer path is deliberately distinct. The pinned mGBA baseline initializes its words to `0xFFFF`, schedules the zero-peer duration, and then installs zero words with error clear at common completion. The network error table must not be applied to that compatibility path.

Reset and unload are different: they cancel pending events and tear down immediately because the reset/unloaded machine cannot observe a completion IRQ.

An idle detach has no transfer completion and raises no SIO IRQ. At the detach barrier—or immediately on an abrupt local transport failure—the driver synchronously installs the hardware-characterized disconnected line state for SIOCNT ID/ready/slave and RCNT SC while busy is already clear. It preserves the communication-error flag and receive words because no transfer completed. Register reads immediately after detach must observe disconnected state without requiring a later SIOCNT write.

### 12. Use operation-specific bounded waits and transport generations

The adapter stores frontend function pointers only for the current transport generation. After every `poll_receive_fn` invocation it immediately drains copied inbound packets, then rechecks the generation and stop flag before reading or calling any saved pointer.

Copied inbound and outbound queues are bounded. Queue exhaustion, an oversized copied packet, outbound/send failure, or impossible callback ordering immediately invalidates the transport generation. Silent dropping is forbidden. The resulting failure enters the pending-session, idle-detach, pre-START no-peer, or post-START immutable-cycle path according to current state.

Separate configurable deadlines cover:

- initial HELLO/accept handshake;
- attachment barrier;
- mode barrier;
- transfer readiness;
- transfer commit;
- transfer-completion catch-up/readiness;
- completion-decision delivery;
- graceful detach.

Defaults are selected from spike measurements and each remains below a three-second safety ceiling. Tests inject a fake monotonic clock and configure each deadline independently. User messages can say “link timed out,” while logs identify the operation and generation.

The production defaults selected from localhost and two-device Wi-Fi
qualification are:

| Operation | Default |
| --- | ---: |
| Handshake | 1,500 ms |
| Attachment | 3,000 ms |
| Grant | 1,500 ms |
| Mode barrier | 1,500 ms |
| Transfer readiness | 3,000 ms |
| Transfer commit | 3,000 ms |
| Completion catch-up | 3,000 ms |
| Completion readiness | 3,000 ms |
| Completion decision/acknowledgement | 3,000 ms |
| Graceful detach | 1,000 ms |

The candidate horizon is one GBA video frame (`280896` cycles), the local
scheduler quantum remains `4096` cycles, and periodic health barriers remain
disabled. These are runtime policy and do not alter wire compatibility.

No worker thread calls frontend functions, no SIO/session lock is held across a frontend callback, and an empty poll loop yields. Missing receive polling prevents attachment.

### 13. Protect every live session state from time and configuration changes

RetroArch disables time manipulation for Netpacket sessions, and the core additionally rejects both `retro_serialize` and `retro_unserialize` whenever session state is anything other than `DISCONNECTED`, including transport started, HELLO exchange, accepted, attachment barrier, ready, transferring, and failed-but-not-torn-down states. This prevents creating a state containing attached SIO registers, busy state, or a scheduled completion without the deliberately excluded transport/session context. `retro_reset` first tears down every non-disconnected transport/session state, then resets. Serialization outside network play is unchanged; transport pointers, queues, sequence counters, and peer state never enter mGBA savestates.

Core-variable update handling and libretro cheat APIs enforce the same lifetime. A timing-sensitive change requested while non-disconnected is ignored/rejected with a diagnostic and cannot partially update the emulation profile. The user must disconnect before applying it.

### 14. Test integration risk and causal behavior in dependency order

Testing proceeds in layers:

- The Phase 0 spike proves current RetroArch lifecycle and Android installation before production architecture.
- Common SIO tests compare every unsupported mode with an otherwise identical no-driver core and characterize error-completion registers.
- Codec tests and fuzzing cover all messages, sizes, fields, and independent counters.
- Atomic-session tests cover quiescent rendezvous, initial mode snapshots, bilateral HELLO, final-ack pause, every interrupted handshake edge, policy/profile mismatch, frozen settings, duplicates, transport generations, bounded-queue failures, and operation-specific deadlines.
- Grant and mode tests vary delivery latency around fixed logical events and prove host-leading single-flight order, non-blocking deferred transfer intents, frozen START/mode tie ordering, identical committed cycles, and identical host observations.
- Two-core driver tests cover every baud, separate topology/effective counts, the complete start-state table, start/ready/commit/abort/catch-up/decision barriers, client progress from `T` to `C`, normal common completion, immutable post-START cycles, terminal final-message outcomes, idle detach cleanup, and IRQ counts.
- A redistributable GBA test ROM records player IDs, mode readiness, words, timing, errors, and IRQs.
- Deterministic trace replay and fault injection vary delivery latency/jitter, delay/duplicate messages, and stop at every poll/barrier phase.
- Linux localhost, real LAN, and Android Wi-Fi qualification follow, with commercial ROMs used only for manual smoke tests.

## Risks / Trade-offs

- [Host-leading single-flight grants add a round-trip stall per epoch] → Accept the correctness-first cost, measure it in the spike and trace harness, keep hard barriers event-driven, and tune only candidate horizon cadence after correctness.
- [A client mode intent can race a host transfer] → Pause and resolve intents before START; defer without blocking any intent after START, mark the transfer abort-pending, and commit the deferred generation after completion.
- [The client is stranded at transfer start while the host runs to completion] → Make only a host-at-`C` `COMPLETION_CATCHUP` authorize client execution to `C`, then exchange ready and decision messages.
- [The final completion decision is lost during terminal failure] → Define the decision as the outcome commit point, guarantee equality only through healthy delivery, and test permitted role-specific terminal observations.
- [The Netpacket ABI is newer than mGBA's vendored header] → Pin the canonical header and prove Linux/Android lifecycle in Phase 0.
- [Stop re-enters receive polling and invalidates callbacks] → Increment transport generation in stop, drain after every poll, and never reuse pointers without a generation check.
- [Common mode-gating changes regress other SIO drivers] → Keep the predicate small, preserve `setMode` notification, and compare supported and unsupported paths across existing drivers.
- [Error completion differs from hardware] → Freeze a register transition table from documentation/no-peer/hardware characterization before transfer coding and test every observable bit.
- [Configuration differences cause game-specific divergence] → Reject mismatched versioned determinism profiles while keeping save memory and inputs independent.
- [Runtime setting changes invalidate an accepted profile] → Freeze timing-sensitive options and cheats throughout every non-disconnected session state.
- [A copied queue or send path silently loses a reliable packet] → Bound queues, fail closed immediately, and select failure timing by whether START was emitted.
- [The fork becomes difficult to rebase] → Preserve upstream ancestry, isolate modules, avoid local-lockstep format changes, and keep the common dispatch patch upstream-quality.
- [Future cross-title link combinations need different ROMs] → Exchange identity independently and reserve compatibility policy/group fields while supporting only `EXACT_ROM` now.

## Migration Plan

1. Establish Git ancestry from the pinned upstream mGBA commit and commit the planning files on the feature branch.
2. Build/test the upstream baseline and refresh the pinned canonical libretro header.
3. Complete the Linux and Android Netpacket feasibility spike and record measured lifecycle/timing constraints.
4. Add and test common SIO mode-gated dispatch plus the error-completion register characterization.
5. Implement the codec, independent sequence domains, determinism profile, and atomic session handshake.
6. Implement host-leading, single-flight frame-oriented grants and barrier-committed initial/runtime mode readiness.
7. Add `GBASIONetplayDriver` successful transfer, explicit catch-up/decision completion, deferred in-transfer modes, exhaustive start-state handling, and immutable-cycle post-START error paths.
8. Add the test ROM, two-core harness, deterministic trace replay, and fault injection.
9. Qualify localhost, LAN, and Android, then tune grant/deadline runtime policy from measurements.

Rollback removes Netpacket registration and new network modules while retaining any independently accepted common SIO dispatch fix. Existing local lockstep formats and user data require no migration.

## Resolved Qualification Questions

- Production uses a one-frame candidate horizon, no periodic health barrier,
  and the operation defaults listed above.
- The CC0 purpose-built ROM is supplemented by afska's independently authored
  MIT-licensed LinkCable `basic` example from release `v8.0.3`. It continuously
  exercised rapid back-to-back transfers on two stock Android RetroArch
  devices and exposed the timing-pass pause defect that the slower fixture had
  masked.
