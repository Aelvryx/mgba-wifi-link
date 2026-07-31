## 1. Preserve Upstream History and Baseline

- [x] 1.1 Initialize the repository, add `upstream` for `mgba-emu/mgba`, fetch the selected commit containing the current lockstep rewrite and ROM checksum support, and create the feature branch from that commit while preserving the existing planning files.
- [x] 1.2 Commit or cherry-pick the OpenSpec and project-control files on top of the pinned upstream ancestry rather than importing an mGBA source snapshot.
- [x] 1.3 Record the upstream URL, commit, source branch, fork branch, import/cherry-pick procedure, and patch-stack conventions in `UPSTREAM.md`.
- [x] 1.4 Build the unmodified upstream libretro core and run available tests, recording exact Linux commands and results as the regression baseline.
- [x] 1.5 Refresh `src/platform/libretro/libretro.h` from a pinned canonical libretro revision containing command 78 and the current Netpacket callback signatures, record that revision, and verify the baseline still builds and loads.

## 2. Complete the Netpacket and Android Feasibility Spike

- [x] 2.1 Add an isolated temporary command-78 adapter that registers from `retro_load_game` and exchanges a minimal reliable flushed ping/ack without adding a production cable protocol.
- [x] 2.2 Run two stock current RetroArch instances on Linux localhost and record callback order, client IDs, protocol-version comparison, packet cadence, RTT distribution, and longest observed wait.
- [x] 2.3 Invoke `poll_receive_fn` from a controlled mGBA timing boundary and verify synchronous receive delivery, inbound-queue processing, and synchronous stop re-entry without reusing invalidated callbacks.
- [x] 2.4 Build and install the skeletal core on two Android devices, repeat ping/ack and stop-during-poll tests over Wi-Fi, and record custom-core installation and host/join steps.
- [x] 2.5 Record the frontend/core versions, required callbacks, Android ABI results, callback cadence, RTT/stall measurements, and feasibility verdict in a spike report.
- [x] 2.6 Remove or quarantine temporary spike code from production build paths after preserving reusable lifecycle tests and findings.

## 3. Harden and Characterize Common GBA SIO

- [x] 3.1 Add a common predicate for whether the attached driver handles the active SIO mode, with explicit semantics for a missing driver or missing `handlesMode` hook, and define a common-owned/driver-owned start result carrying the effective transfer peer count.
- [x] 3.2 Audit and apply the predicate before driver-specific start, connected-device, mode-visible device-ID, SIOCNT, RCNT, and mode-specific register hooks; consume the explicit effective count for scheduling, latch each transfer's mode, and gate finish dispatch by that completion mode rather than a later current mode while retaining mode-change lifecycle notification.
- [x] 3.3 Add differential tests proving NORMAL8 and NORMAL32 start/timing/completion/register/IRQ behavior with the MULTI-only network driver attached exactly matches a no-driver core.
- [x] 3.4 Add differential tests for UART, GPIO, Joy Bus, RCNT, SIOCNT, and mode-specific register writes, proving unsupported modes never call network hooks.
- [x] 3.5 Run existing local lockstep and other SIO-driver tests to verify supported-mode dispatch and serialized local-driver behavior remain unchanged.
- [x] 3.6 Characterize ordinary no-peer start, non-initiating secondary start, idle detach, and erroneous MULTI completion using the GBA programming manual, mGBA's no-driver path, and an available hardware trace or conformance fixture; freeze tables for words, busy, error, ready, slave, ID, RCNT SC, timing, completion-event creation, and IRQ.
- [x] 3.7 Freeze and test ordering for scheduled remote START versus a CPU mode write at the same cycle and for already-due completion versus a CPU mode write at the same cycle, plus latched MULTI finish dispatch when current mode changes before completion, across network, local-lockstep, and dummy-driver paths.
- [x] 3.8 Reconcile the OpenSpec start, detach, and abort requirements before transfer coding if characterization contradicts any specified register value or completion timing.

## 4. Implement the Protocol Codec and Atomic Session

- [x] 4.1 Add build-system entries and module boundaries for the explicit codec, transport-neutral session, production Netpacket adapter, and `GBASIONetplayDriver` without changing default behavior.
- [x] 4.2 Define the fixed protocol string, exact wire version, capability bits, compatibility-policy enum, stable error/abort reason codes, packet and copied-queue limits, and messages for bilateral handshake, grants, mode barriers, `TRANSFER_START`/`READY`/`COMMIT`/`ABORT`, `COMPLETION_CATCHUP`/`READY`/`DECISION`/`DECISION_ACK`, and detach.
- [x] 4.3 Define independent 64-bit domains for per-sender packets, session IDs, execution grants, mode generations, transfers, completion barriers, and optional health barriers, with clean-detach behavior instead of wrap.
- [x] 4.4 Implement little-endian encoders for every message without packed structs, implicit padding, or uninitialized bytes.
- [x] 4.5 Implement decoders that validate magic, exact version, lengths, type, sender role, transport generation, session ID, field ranges, and message-specific sequence relations before returning typed data.
- [x] 4.6 Add codec golden-vector, round-trip, byte-truncation, trailing-data, oversized-payload, invalid-enum, invalid-ID, counter-boundary, and conflicting-duplicate tests.
- [x] 4.7 Add a parser fuzz target or randomized equivalent proving arbitrary bounded input cannot access out of bounds or partially mutate session state.
- [x] 4.8 Implement the transport vtable, copied bounded queues, local transport-generation invalidation, reliable send/flush, receive polling, monotonic time, diagnostics, and stop operations without including `libretro.h` in GBA SIO modules; fail closed immediately on queue exhaustion, oversized copied packets, send failure, or impossible callback ordering.
- [x] 4.9 Compute effective-ROM SHA-1 and byte length and add tests covering memory-loaded, full-path, extracted, and frontend-patched content where supported.
- [x] 4.10 Define a stable `emulation_compatibility_version` and protocol-canonical per-category determinism digests for BIOS/HLE and BIOS identity, CPU/timing/idle/overclock/speed-hack options, RTC override mode, and disabled-cheat state while excluding arbitrary serialized configuration, compiler/ABI/build metadata, save memory, inputs, visual/audio settings, and runtime RTC value.
- [x] 4.11 Implement bilateral session-ID-zero `HELLO` exchange from paused quiescent SIO rendezvous boundaries so both peers independently learn protocol, capabilities, identity, supported policies, compatibility version, determinism categories, and initial local mode snapshots.
- [x] 4.12 Implement MVP `EXACT_ROM` selection and rejection of mismatched ROMs or unsupported `COMPATIBILITY_GROUP`, keeping identity and policy fields separate.
- [x] 4.13 Implement header-session-zero `ACCEPT` carrying a proposed nonzero session ID, followed by nonzero-session `ACCEPT_ACK`, `SESSION_READY`, and `SESSION_READY_ACK` at a common attach cycle; wait for pending no-peer SIO completion, require busy-clear quiescence, and fail the attachment deadline rather than installing over active SIO state.
- [x] 4.14 Implement exact-duplicate idempotency/response replay, conflicting-duplicate failure, stale transport-generation rejection, and all invalid handshake transitions.
- [x] 4.15 Implement separately configurable handshake, attachment, mode, transfer-readiness, transfer-commit, completion-catch-up/readiness, completion-decision-delivery, and detach deadlines under a three-second ceiling, processing the inbound queue and rechecking generation after every poll.
- [x] 4.16 Keep player one paused after `SESSION_READY_ACK` until the first valid post-attachment initial-mode commit or grant, keep player zero at the attach cycle until the acknowledgement, and scope packet-latency determinism from the quiescent rendezvous onward.
- [x] 4.17 Reject timing-sensitive core-variable and cheat API changes in every non-disconnected state without partially changing the accepted profile; preserve updates to explicitly excluded visual/non-timing options.
- [x] 4.18 Add fake-transport session tests for every successful and interrupted handshake edge, pending SIO completion, both/already-one/neither MULTI snapshots, mode-write rendezvous ordering, final-ack pause, policy/profile mismatch, harmless build differences, frozen configuration, third-player rejection, missing polling, queue/send failure, sequence exhaustion, duplicate rules, stop during poll, timeout category, and clean teardown.

## 5. Implement Conservative Grants and Committed Mode Readiness

- [x] 5.1 Implement a monotonic 64-bit virtual cable clock and wrap-safe mappings to each local `mTiming` domain.
- [x] 5.2 Implement the fine local scheduler quantum as an internal queue check that emits no packet solely because the quantum elapsed.
- [x] 5.3 Implement host-leading `EXECUTION_GRANT` and client `GRANT_ACK`: player zero executes first to a test-configurable candidate horizon, issues a grant only after reaching it, and remains there until resolution.
- [x] 5.4 Enforce at most one outstanding grant, prevent player one from holding or passing a grant beyond player zero's current cable cycle, and prevent player zero from leaving the granted horizon until acknowledgement or earlier client intent resolves.
- [x] 5.5 Submit the quiescent attachment snapshots as the initial `MODE_INTENT` generation, implement ordinary pre-transfer intents with originator pause, and implement post-START intents as non-blocking deferred generations that mark abort-pending and commit only after completion.
- [x] 5.6 Implement host-selected `MODE_COMMIT` at a deterministic unpassed grant boundary and bilateral `MODE_ACK`; keep player one paused after its acknowledgement until player zero installs both acknowledgements and releases the next grant.
- [x] 5.7 Truncate the candidate horizon at any earlier host transfer start; make START a catch-up barrier and make a client intent at `C < T` take precedence over readiness at `T`.
- [x] 5.8 Keep periodic health barriers disabled by default; expose any test-enabled cadence as runtime policy rather than protocol compatibility.
- [x] 5.9 Add grant/mode tests for host/client entry and exit, initial snapshots, delayed intent, one-grant limit, host-never-behind invariant, pre-START intent races, same-cycle START-first ordering, non-blocking host/client post-START intents, intent discovered after host reaches completion, start truncation, missing acknowledgements, impossible past events, final-ack pauses, and operation-specific timeouts.
- [x] 5.10 Replay identical logical mode writes and host starts with varied bounded delivery latency/jitter and assert identical commit boundaries, no grant beyond START, no valid START in the client past, ready-bit observations, host control flow, and trace output.

## 6. Add the Network GBA SIO Driver and Transfers

- [x] 6.1 Add `GBASIONetplayDriver` declarations and lifecycle beside `GBASIOLockstepDriver`, accepting only MULTI and remaining transport-neutral.
- [x] 6.2 Implement quiescent atomic attach/detach, session-level `topological_peer_count`, SIO line readiness, player-zero primary ID, player-one secondary ID, initial/later committed mode state, and immediate characterized idle-detach SIOCNT/RCNT cleanup; never use topology as transfer timing count.
- [x] 6.3 Implement the host `start()` state table and explicit common start result: effective count zero before joint readiness or START emission, effective count one after START emission including abort-pending, plus reset/unload cancellation without temporary `connectedDevices()` mutation.
- [x] 6.4 Implement client remote-start handling to prove the start is not in its past, give an earlier local mode intent precedence, or otherwise map `T`, set busy, retain `C`, capture `SIOMLT_SEND`, schedule the existing completion event, send exactly one `TRANSFER_READY`, and remain paused at `T` after COMMIT.
- [x] 6.5 Validate readiness and send candidate `TRANSFER_COMMIT` with `[player0, player1, 0xFFFF, 0xFFFF]` without treating it as a grant, then return a common-owned start result with effective count one so host completion is scheduled at `C`.
- [x] 6.6 Implement commit validation and idempotency for stale/skipped/future/conflicting sequences and wrong start or completion cycles.
- [x] 6.7 Implement host-led completion: host executes `T→C`, sends `COMPLETION_CATCHUP` from its finish hook, client executes `T→C`, sends `COMPLETION_READY` and waits, host sends authoritative `COMPLETION_DECISION`, client installs it and sends `COMPLETION_DECISION_ACK`, and each hook returns only at its specified release point without duplicating common register/IRQ completion.
- [x] 6.8 Implement `GBASIONetplayAbortTransfer()` and `TRANSFER_ABORT`: pre-emission failures use effective count zero/no-peer timing; post-emission failures preserve `C` and effective count one for host scheduling; transport loss before catch-up lets an accepting client advance locally only to `C`; and final-decision loss follows the specified role-specific terminal outcome.
- [x] 6.9 Produce the characterized error state through the finish/common path: all `0xFFFF` words, error set, busy clear, ready one, slave one, ID zero, RCNT SC high, one enabled IRQ, and zero peers for later writes.
- [x] 6.10 Implement host/client mode departure after START as deferred non-blocking intent, immutable-cycle error completion, and mode commit before the next grant; preserve START-before-write and completion-before-write tie ordering; keep reset/unload cancellation immediate without a fabricated IRQ.
- [x] 6.11 Add two-driver tests for every MULTI baud, words, exact cycles, topology/effective counts, client not stranded at `T`, catch-up authorisation, decision release, busy transitions, IDs, unused slots, IRQ disabled/enabled behavior, common scheduling ownership, player-one independent start, not-ready host start, duplicates, and invalid sequences.
- [x] 6.12 Add the complete failure matrix: pre-START failure; START received and READY lost; stop before READY; COMMIT send failure; stop after COMMIT; lost/delayed CATCHUP; lost READY; decision send rejection; transport accepted decision but client delivery failed; client received decision before stop; delayed ABORT; START undelivered; deferred host/client mode writes; queue/send/protocol failure; reset/unload; and subsequent SIO write.

## 7. Integrate the Production Libretro Adapter

- [x] 7.1 Replace the spike adapter with production command-78 callbacks and the fixed protocol-version string, registering from `retro_load_game` only for valid GBA content.
- [x] 7.2 Implement `start`, `receive`, `poll`, `connected`, `disconnected`, and `stop` with current reliable, flush-hint, client/broadcast, receive-poll, and transport-generation semantics, including an unambiguous adapter-level definition of successful START emission and fail-closed send failure.
- [x] 7.3 Connect production adapter/session state to network-driver creation, atomic attachment, failure completion, and destruction while preserving dormant single-player fallback.
- [x] 7.4 Process copied inbound packets immediately after every poll and prove synchronous stop prevents every later access to invalid send/poll pointers.
- [x] 7.5 Route readiness, assignments, compatibility/profile errors, protocol errors, operation-specific timeouts, and detach outcomes through mGBA logging and frontend messages without payload leakage.
- [x] 7.6 Ensure `retro_unload_game` tears down before core/ROM destruction, `retro_reset` tears down every non-disconnected state before reset, and both `retro_serialize` and `retro_unserialize` reject every state from `TRANSPORT_STARTED` through failed-but-not-torn-down.
- [x] 7.7 Wire core-variable updates and cheat APIs to the session freeze, rejecting timing-sensitive changes before they affect emulation while allowing explicitly excluded settings.
- [x] 7.8 Add fake-libretro tests for registration support, host/client startup, admission limits, callback order, packet copying, queue exhaustion, oversized packets, send failure, flush/poll ordering, stop re-entry, generation invalidation, frozen variables/cheats, rejected state save/load and reset in every non-disconnected state, disconnected serialization, and unload.

## 8. Add Test ROM, Deterministic Tracing, and Fault Injection

- [x] 8.1 Create a redistributable minimal GBA link-test ROM source tree with reproducible build instructions while keeping its ARM toolchain outside normal core dependencies.
- [x] 8.2 Implement test-ROM reporting for observable attachment, committed mode readiness, player ID, effective participant count, every MULTI baud, incrementing words, returned words, busy/error/ready/slave state, completion timing, and missed or duplicate IRQs; report topology separately in harness traces.
- [x] 8.3 Build an in-memory two-core harness that boots the ROM twice, supplies independent input/save memory, connects fake transports, and asserts the ROM result block.
- [x] 8.4 Capture transport generation, runtime timing policy, topology/effective counts, every sequence domain, logical cable cycle, packet transition, outcome commit point, deferred mode state, register result, and IRQ in deterministic traces.
- [x] 8.5 Implement trace replay with varied bounded latency/jitter and assert identical healthy-generation attachment, grant, mode, transfer, completion decision, post-START error, host-observation, and IRQ outcomes while separately asserting permitted terminal final-decision asymmetry.
- [x] 8.6 Inject delayed reliable delivery, exact/conflicting duplicates, withheld messages, queue exhaustion, oversized packets, send failure, sequence exhaustion, and stop/disconnect at every handshake, grant, mode, START, TRANSFER_READY/COMMIT/ABORT, COMPLETION_CATCHUP/READY/DECISION, and detach phase.
- [x] 8.7 Verify all injected failures respect their operation-specific deadline, never deadlock, never leave busy set forever, and never install guessed successful data.

## 9. Qualify, Tune, and Document the MVP

- [x] 9.1 Run codec, session, SIO, driver, adapter, integration, sanitizer, fuzz/randomized, and deterministic-replay suites and fix all safety, leak, deadlock, and determinism failures.
- [x] 9.2 Re-run the upstream baseline and prove ordinary single-player, savestates outside netplay, every unsupported SIO mode, and existing local lockstep remain unchanged.
- [x] 9.3 Run two stock current RetroArch instances with the completed core and link-test ROM over Linux localhost and compare results with the feasibility spike.
- [x] 9.4 Run the same test over a real LAN and document RTT, grant stalls, mode/transfer barrier stalls, timeout behavior, packet rates, and trace determinism.
- [x] 9.5 Build supported Android libretro ABIs and complete two-device Wi-Fi qualification with the test ROM, then smoke-test a representative independently authored Multi-Pak workload without committing commercial ROMs.
- [x] 9.6 Select and document production candidate-horizon cadence and per-operation deadline defaults from Linux/Android measurements, leaving host-leading single-flight order, local scheduler values, and protocol compatibility unchanged.
- [x] 9.7 Document upstream/header pins, installation, host/join flow, required frontend version/callbacks, exact-ROM MVP policy, compatibility/determinism rules, frozen settings and serialization, diagnostics, timeout categories, observable attachment, topology/effective counts, idle-detach registers, START point-of-no-return, completion catch-up/decision and terminal guarantee, deferred transfer modes, abort-visible registers, and every exclusion.
- [x] 9.8 Run strict OpenSpec validation and review every scenario in both capability specs against automated or recorded manual evidence before marking implementation complete.
