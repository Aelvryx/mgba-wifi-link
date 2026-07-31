## 1. Preserve the measured baseline

- [x] 1.1 Add a sanitized diagnostic report for the Odin/Thor runs with exact packet counts, grant RTTs, emulated-frame rate, serial-word rate, transfer-phase latency, audio starvation, CPU, and Wi-Fi observations.
- [x] 1.2 Add a reusable log-analysis script that derives those metrics from sampled and unsampled protocol traces without mistaking printed lines for packet counts.
- [x] 1.3 Add a protocol-v1 integration fixture that reproduces single-flight frame-grant blocking and asserts the number of real core frames separately from frontend `retro_run()` calls.
- [x] 1.4 Add an audio fixture that records generated samples and detects repeated blocked calls returning no new emulated audio.

## 2. Add the bounded protocol-v1 pacing correction

- [x] 2.1 Add a generation-safe adapter helper that polls receive and drains copied packets until execution becomes runnable, stop invalidates the generation, or a supplied deadline expires.
- [x] 2.2 Use the helper for client grant catch-up inside `RunBegin` so a healthy blocked call advances one emulated frame before returning.
- [x] 2.3 Use the helper for host grant acknowledgement inside `RunEnd` so the next frontend call does not serialize host/client computation across display callbacks.
- [x] 2.4 Preserve synchronous receive/stop re-entry safety by revalidating saved callback pointers and transport generation after every poll.
- [x] 2.5 Add tests for grant arrival, stop during poll, unavailable polling, deadline expiry, and zero stale-callback invocations.
- [x] 2.6 Confirm the v1 pacing fixture eliminates recurring empty-audio returns under the recorded LAN RTT model, records the remaining host-led frame-rate ceiling, and does not treat v1 as the real-time release gate.

## 3. Build the transport-independent replicated-pair spike

- [x] 3.1 Add a test-only pair harness that constructs two GBA cores from one ROM image with independent state/save backing and deterministic logical IDs.
- [x] 3.2 Attach two existing `GBASIOLockstepDriver` instances to a fresh local coordinator in P0-then-P1 order.
- [x] 3.3 Implement the two-worker scheduling candidate using existing mGBA lockstep thread users while keeping all libretro callbacks outside worker execution.
- [x] 3.4 Implement the cooperative scheduling candidate whose lockstep sleep/wake callbacks yield between cores at mTiming boundaries.
- [x] 3.5 Drive both scheduler candidates with the continuous-transfer fixture for at least ten emulated seconds and record frame, word, wait, and state-trace counters.
- [x] 3.6 Add deterministic teardown tests at idle, mode transition, transfer start, transfer completion, reset, and partial core-construction points.
- [x] 3.7 Run normal, ASan, UBSan, and TSan where supported; eliminate deadlocks, leaks, data races, stale events, and nondeterministic trace hashes.
- [x] 3.8 Select one scheduler in an architecture-decision note based on determinism, upstream fit, measured throughput, and teardown behavior.

## 4. Gate the architecture on Android feasibility

- [x] 4.1 Expose the selected pair spike behind a non-release diagnostic core option without changing the default protocol runtime.
- [x] 4.2 Build the exact Android arm64 diagnostic core reproducibly and record its source revision and SHA-256.
- [x] 4.3 Install the diagnostic core on the Odin and Thor without modifying their normal configuration or save paths.
- [x] 4.4 Run the continuous-transfer fixture on each device for at least ten minutes and capture real emulation FPS, serial words per emulated second, wall CPU, peak memory, temperature, throttling state, and teardown latency.
- [x] 4.5 Compare Android serial throughput with the same build's Linux and Android local-lockstep baseline and investigate any gap above five percent.
- [x] 4.6 Record a go/no-go decision: proceed only if both devices sustain at least 59 emulated FPS without sustained thermal throttling; otherwise stop production v2 work and open a replacement architecture proposal.

## 5. Define and implement canonical replica bundles

- [ ] 5.1 Inventory mutable GBA state required to resume one logical cartridge, including CPU/memory, timing events, SIO, save-memory type/data, GPIO peripherals, and RTC mode/state.
- [ ] 5.2 Define a versioned, endian-stable replica manifest and bundle format that excludes ROM bytes, frontend paths, presentation buffers, and network state.
- [ ] 5.3 Implement bounded bundle creation at a quiescent local-core boundary without mutating the source core.
- [ ] 5.4 Implement chunked bundle assembly with declared resource ceilings, overlap/hole checks, exact duplicate rules, and compressed/uncompressed digests.
- [ ] 5.5 Implement fresh-core restoration from a verified bundle using the already identity-checked effective ROM.
- [ ] 5.6 Add round-trip tests for SRAM, Flash variants, EEPROM variants, no-save carts, RTC state, GPIO peripherals, different local cycles, and games already in MULTI mode.
- [ ] 5.7 Add malformed-manifest, oversize, truncated, overlap, decompression-bomb, digest-mismatch, wrong-player, and wrong-generation tests.

## 6. Add protocol-v2 codec and attachment state machine

- [ ] 6.1 Increment the exact protocol compatibility version and add explicit v2 runtime/capability negotiation without automatic downgrade.
- [ ] 6.2 Add fixed-width codecs for replica manifest, replica chunk, replica-installed, session-ready policy, input batch, state check, and v2 detach messages.
- [ ] 6.3 Validate canonical Booleans, roles, player ownership, payload lengths, reserved fields, packet sequence, snapshot generation, chunk relation, input frame, and state-check frame before exposing typed data.
- [ ] 6.4 Start the attachment deadline at peer admission and capture each assigned-player bundle at the first quiescent boundary.
- [ ] 6.5 Exchange and verify both authoritative bundles while retaining the original single core as the rollback source.
- [ ] 6.6 Construct provisional P0/P1 pairs identically on both endpoints and acknowledge their bundle digests before observable attachment.
- [ ] 6.7 Implement `SESSION_READY`/ack and the first input-window release so neither endpoint executes the pair one-sided.
- [ ] 6.8 Add fault injection at every manifest, chunk, installation, and final-readiness send/receive boundary, including synchronous stop and queue exhaustion.

## 7. Promote the selected scheduler into `GBAReplicatedPair`

- [ ] 7.1 Create a transport-independent pair module owning two cores, coordinator, lockstep drivers/users, logical assignments, frame number, and deterministic lifecycle.
- [ ] 7.2 Move the selected spike scheduler into the pair module and delete the unselected production candidate while retaining comparative evidence.
- [ ] 7.3 Add APIs to install authoritative P0/P1 bundles, set both frame inputs, run to the next common frame boundary, query role output, and stop safely.
- [ ] 7.4 Ensure SIO mode changes and every MULTI transfer use only the local lockstep coordinator in protocol v2.
- [ ] 7.5 Add per-frame deterministic state traces and local SIO transfer/word/wait counters for tests and diagnostics.
- [ ] 7.6 Verify common SIO mode gating, completion ownership, player IDs, line state, receive words, busy clearing, and IRQ behavior against existing local-lockstep tests.

## 8. Implement frame-input synchronization

- [ ] 8.1 Negotiate a fixed whole-frame input delay from a versioned supported range and recorded handshake RTT/jitter, and freeze it for the session.
- [ ] 8.2 Add bounded per-player input rings keyed by 64-bit replicated frame number with ownership checks and exact/conflicting duplicate handling.
- [ ] 8.3 Sample only the assigned physical controller and author it for frame `F + D` with a short redundancy window in one reliable flushed `INPUT_BATCH`.
- [ ] 8.4 Release frame `F` only after both authoritative inputs exist, set both cores once, and advance the pair exactly once.
- [ ] 8.5 Integrate bounded in-call receive rendezvous so a healthy `retro_run()` returns one newly advanced local-role frame with normal audio/video.
- [ ] 8.6 Add deterministic jitter, reordering-within-ordered-delivery, duplicate, conflicting duplicate, missed-input, timeout, and stop-during-wait tests.
- [ ] 8.7 Assert ordinary v2 packet counts scale with frames and remain unchanged when fixture serial-word volume increases.

## 9. Route frontend behavior by logical role

- [ ] 9.1 Route host video/audio to P0 and client video/audio to P1 while draining shadow output without frontend callbacks.
- [ ] 9.2 Route controller, rumble, sensors, camera, solar, and other frontend-facing peripheral behavior only through the assigned logical core.
- [ ] 9.3 Prevent the shadow core from reading frontend input or invoking duplicate environment callbacks.
- [ ] 9.4 Add host/client presentation tests with visibly and audibly distinct logical-player fixtures.
- [ ] 9.5 Confirm a runnable protocol-v2 call generates a normal local-role audio buffer and add a regression for Android-style empty-buffer stop storms.

## 10. Enforce save ownership and safe restoration

- [ ] 10.1 Give the local-role core the endpoint's normal save backing and give the shadow core memory-only save backing with no frontend path.
- [ ] 10.2 Track per-logical-player save dirty generations and last verified pair frame.
- [ ] 10.3 On clean teardown, copy only the newest verified local-role save/state back to the retained or restored single core.
- [ ] 10.4 On divergence or uncertain teardown, preserve the last valid local-owned generation and emit a diagnostic instead of writing uncertain data.
- [ ] 10.5 Add host/client tests proving P0 persists only on host, P1 only on client, and no shadow save reaches disk for clean, timeout, stop, reset, and unload paths.
- [ ] 10.6 Continue rejecting serialize/unserialize in every non-disconnected v2 state and teardown before reset.

## 11. Add replica verification and diagnostics

- [ ] 11.1 Define a versioned canonical digest input for future-affecting GBA state that excludes host-specific and presentation-only fields.
- [ ] 11.2 Compute P0/P1 digests at the negotiated interval and exchange one `STATE_CHECK` for the exact replicated frame.
- [ ] 11.3 Fail closed before releasing later input when a digest differs and log frame, logical player, digests, recent inputs, runtime policy, and session ID without private paths or save bytes.
- [ ] 11.4 Add structured counters for real pair frames, rendezvous duration, input depth, packets/bytes, RTT/jitter, queue high-water marks, local SIO words/waits, audio production, and per-core runtime.
- [ ] 11.5 Emit concise attach, periodic, and teardown summaries while keeping verbose packet traces opt-in and sampled.
- [ ] 11.6 Add injected single-bit divergence tests for P0, P1, save memory, timing state, and excluded presentation state.

## 12. Complete automated validation

- [ ] 12.1 Add unit suites for replica bundle, replicated pair, v2 codec, v2 session, input rings, save ownership, verification, and libretro role routing.
- [ ] 12.2 Add end-to-end paired-adapter replay with latency/jitter/fault injection at every attachment, input, state-check, detach, reset, and unload boundary.
- [ ] 12.3 Run the continuous-transfer ROM for at least 30 emulated minutes and assert identical per-frame traces, no deadlock, and local-baseline serial throughput.
- [ ] 12.4 Run focused normal, ASan/UBSan, leak, and supported thread-sanitizer suites in CI with pinned toolchains and fail-fast settings.
- [ ] 12.5 Add fixture reproducibility and protocol-v2 packet-count checks to CI.
- [ ] 12.6 Retain protocol-v1 trace tests as a short SIO correctness oracle and mark its runtime selection diagnostic-only.

## 13. Qualify the exact Android release candidate

- [ ] 13.1 Build and hash the exact arm64 release candidate from the reviewed head and install only that core on both devices.
- [ ] 13.2 Run a 30-minute continuous-transfer session and verify at least 59 displayed/emulated FPS, matching state checks, normal audio delivery, frame-scaled packets, and serial throughput within five percent of local baseline.
- [ ] 13.3 Run fresh-connect, multiplayer entry, gameplay, idle, peer-stop, and clean-exit scenarios in Bomberman as the fast-entry commercial title.
- [ ] 13.4 Run the equivalent Four Swords qualification and confirm the former linking-screen one-FPS collapse is absent.
- [ ] 13.5 Capture p50/p95/max input rendezvous, negotiated delay, packet/byte rate, CPU, peak memory, temperature, throttling, audio behavior, and save persistence for both devices.
- [ ] 13.6 Confirm normal RetroArch controls/configuration and user saves remain untouched outside the installed release core.

## 14. Release and review hygiene

- [ ] 14.1 Update README usage, limitations, host/join instructions, protocol incompatibility, input latency, diagnostics, save ownership, and safe disconnect guidance.
- [ ] 14.2 Update `UPSTREAM.md`, protocol documentation, test matrix, build provenance, artifact hashes, and exact Android evidence.
- [ ] 14.3 Remove disposable spike switches, test cores, temporary device files, and unsanitized diagnostic artifacts while retaining reproducible fixtures and summarized evidence.
- [ ] 14.4 Rebase the upstream-facing patch stack and split pair infrastructure, bundle/codec, session/input, frontend/save routing, diagnostics, tests, and documentation into reviewable commits.
- [ ] 14.5 Run strict OpenSpec validation, the complete CI matrix, and one final exact-build two-device smoke test before publishing the alpha core.
