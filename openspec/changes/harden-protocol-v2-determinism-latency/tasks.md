## 1. Preserve and characterize the alpha.2 baseline

- [x] 1.1 Record the exact source commit, protocol/runtime versions, Android artifact identity, current two-to-eight-frame range, and current selector formula before modifying code.
- [x] 1.2 Add a deterministic characterization test proving that replica-capture work inside the present ACCEPT/ACK interval inflates the selected delay.
- [x] 1.3 Extract the existing Android evidence for four- and five-frame sessions, input rendezvous, FPS, audio, and packet rate into a latency baseline table.
- [x] 1.4 Audit every current libretro/core setting that can affect future emulated execution and record its canonical determinism category or explicit exclusion rationale.
- [x] 1.5 Audit GBA cartridge hardware flags and frontend peripherals for RTC, tilt, gyro, luminance/solar, rumble, and any other external observation used by supported GBA cores.
- [x] 1.6 Build and run the unchanged focused protocol-v2 suites and preserve the known complete-suite baseline before beginning production changes.

## 2. Implement the canonical determinism profile

- [x] 2.1 Extend the transport-neutral identity module with schema-v1 `uint16` schema/count, at most sixteen ascending 36-byte SHA-256 category records, fixed IDs/required flags, exact domain prefixes/suffixes and payload layouts, little-endian fields, canonical Booleans/reserved bytes, and unknown optional/required semantics.
- [x] 2.2 Add canonical BIOS/HLE identity including a BIOS digest only when a real BIOS is active.
- [x] 2.3 Add canonical CPU timing, overclock, speed-hack, and emulation-compatibility fields with checked supported ranges.
- [x] 2.4 Add canonical idle-optimization and opposing-direction/input-filter policy fields.
- [x] 2.5 Keep RTC normalization/arithmetic/semantics-model versions and authoritative-input format/required mask in peer-equal categories; advertise supported RTC sources, time semantics, player-owned source, RTC-content requirement, and synchronized-input capabilities separately in `HELLO`.
- [x] 2.6 Exclude presentation, controller mapping, save data, paths, compiler, ABI, and harmless build metadata by construction rather than by post-hash filtering.
- [x] 2.7 Add golden-vector and unit tests for every exact category/domain payload, schema/count and sixteen-record bound, identical bytes across poisoned padding/native layouts, duplicate/out-of-order records, unknown enums/bits/flags, unknown required rejection, and unknown optional ignore semantics.
- [x] 2.8 Add category-by-category equality and diagnostic tests plus capability-superset tests proving unused local RTC/time/input support does not alter peer-equal profile compatibility.
- [x] 2.9 Build the profile from the actual loaded core, effective overrides, BIOS, frontend variables, cheat device, and cartridge hardware rather than hard-coded defaults.

## 3. Make RTC and external-input policy deterministic

- [x] 3.1 Add `SIGNED_64BIT_TIME_T_V1` capability detection and reject RTC-bearing sessions before calibration when either endpoint lacks signed-at-least-64-bit `time_t` semantics while preserving non-RTC portability.
- [x] 3.2 Add a transport-independent helper that samples `RTC_NO_OVERRIDE` and `RTC_WALLCLOCK_OFFSET` once and calculates the equivalent `RTC_FAKE_EPOCH` using the specified frame/cycle/frequency rational expression, checked signed/wide arithmetic, negative-time handling, and complete `UINT32_MAX` frame-domain bounds.
- [x] 3.3 Preserve `RTC_FIXED` and valid existing fake epochs canonically, verify their common signed-64-bit output domain, and reject unsupported custom RTC sources before replica capture.
- [x] 3.4 Normalize each authoritative player's RTC in its replica bundle so both copies of P0 agree and both copies of P1 agree without forcing P0 and P1 to share one source or value.
- [x] 3.5 Extend attachment and verified checkpoints with every original source-policy/value and normalized runtime RTC field needed for transactional teardown.
- [x] 3.6 Implement the per-source teardown table: exact fixed/fake restoration, original default/offset wall-clock semantics with documented possible checkpoint-time jumps, and rejection of custom sources.
- [x] 3.7 Add RTC unit regressions for non-RTC capable/incapable time peers, RTC rejection for that pair, differing supported-source masks whose intersection does/does not contain actual P0/P1 sources, default wall clock, offset, fixed, fake epoch, negative dates, initial-fit/horizon-overflow, every arithmetic/representation boundary, custom source, checkpoint failure, and both teardown generations.
- [x] 3.8 Derive required tilt, gyro, and luminance/solar capabilities from effective cartridge hardware before `HELLO`, including manual solar-control mode.
- [x] 3.9 Reject every required external input absent from the authoritative input format while allowing local rumble output.
- [x] 3.10 Add cartridge-capability tests for digital-only, tilt, gyro, physical solar, manual solar, combined capabilities, rumble-only, missing bilateral required support, and an unused synchronized-input capability present on only one endpoint.
- [x] 3.11 Extend live-session guards to reject every profile, RTC, sensor-policy, cheat, and latency-policy mutation in all non-disconnected states.
- [x] 3.12 Add a redistributable RTC integration fixture that repeatedly reads P0/P1 time, crosses several second boundaries, verifies corresponding replicas, and validates selected source-policy restoration on teardown.

## 4. Version and harden the protocol codec

- [x] 4.1 Increment the experimental runtime compatibility version and define unique connection nonce, canonical profile records, and the exact raw `HELLO` capability layout for player-owned RTC source, RTC-content requirement, `SIGNED_64BIT_TIME_T_V1`, supported-source mask, synchronized-input mask, and latency policy.
- [x] 4.2 Add stable mismatch/failure reasons for profile schema/category, RTC time semantics/normalization/source, external input, calibration timeout/field/identity/clock, `ACCEPT_TIMEOUT`, policy mismatch, and `CALIBRATED_TARGET_OUT_OF_RANGE`.
- [x] 4.3 Add fixed-width `CALIBRATION_BEGIN`, `LATENCY_PROBE`, `LATENCY_ACK`, and full-vector `LATENCY_REPORT` payloads with provisional session identity, generation, role, count, ordinal, unit, and policy semantics.
- [x] 4.4 Carry calibration generation/vector digest, selection-policy version, p50/p95/max statistics, negotiated range, production policy/floor, selected delay, and selection reason through `ACCEPT` and `SESSION_READY`.
- [x] 4.5 Update little-endian encode/decode length tables, canonical Boolean and reserved-byte checks, enum bounds, range relations, and exact session-ID phase rules for every changed packet.
- [x] 4.6 Reject old runtime peers, unsupported profile/policy versions, unknown capability bits, conflicting duplicates, wrong-role calibration messages, and mixed v1/v2 packets before mutable state exchange.
- [x] 4.7 Add codec golden vectors and malformed-field tests for every new payload, the exact profile/category and calibration-vector digest bytes including both nonces, zero and 1,000,000-microsecond boundaries, ordinals 0 and 11, exact twelve-value reports, nonce/provisional identities, and every byte/field boundary.
- [x] 4.8 Add fuzz-style deterministic decode tests for truncation, extension, noncanonical values, integer limits, and reserved data.

## 5. Implement bilateral clean latency calibration

- [x] 5.1 Extend the transport timing abstraction with fallible `monotonicTimeUs(context, out)`, deterministic fake-time, POSIX/Android, and Windows implementations; decline protocol-v2 registration on any build without a conforming source.
- [x] 5.2 Add process-lifetime-unique bilateral HELLO nonces plus host-only unique `CALIBRATION_BEGIN` provisional ID/generation allocation, bind them to both nonces, promote the provisional ID through `ACCEPT`, and fail rather than wrap/reuse.
- [x] 5.3 Implement the exact sequential state/role table: twelve host probes, host report, twelve client probes, client report, client `WAIT_ACCEPT`, host `CALIBRATED`, host `ACCEPT`, and both `ACCEPTED`, with one outstanding probe.
- [x] 5.4 Implement bounded per-role sample storage, exchange both complete reports to the other peer, construct canonical host-then-client vectors, and calculate the domain-separated vector digest on both endpoints.
- [x] 5.5 Fully encode and commit expected ordinal/outstanding state before a successful `T0`, invoke send only afterwards, acquire fallible `T1` after copied/popped/decoded/validated ACK, and send receiver ACK before logging or optional work.
- [x] 5.6 Implement absolute non-refreshing deadlines through the fallible clock: host three-second begin-send through client-report validation, client three-second begin-validation through report-send success, then client three-second `ACCEPT_TIMEOUT`; use checked addition and `now < deadline_at`, and fail clock read, backward time, overflow, timeout, missing/out-of-range data, wrong state/role/identity/ordinal, queue/send/stop/arithmetic/generation faults without fallback.
- [x] 5.7 Prove through an injected slow replica callback and optional diagnostic delay that capture, compression, installation, and logging cannot alter calibration samples.
- [x] 5.8 Test asymmetric endpoint callback delay so the canonical union includes observations initiated by both roles without claiming RTT/2 is a one-way bound.
- [x] 5.9 Test byte-identical semantic `HELLO` and calibration replay under the next global sequence, literal old-sequence rejection, no new nonce/comparison/deadline/second begin, original-ACK reuse without new `T0`, no second close/recalculation, conflicting duplicate, skipped/future ordinal, stale/reused identities, malformed report, missing ACK/report/ACCEPT, independent probe/deadline clock failures, repeated progress/replay without deadline refresh, just-before/at/after absolute boundaries, stop re-entry, and every deadline edge.

## 6. Replace the fixed-delay selector

- [x] 6.1 Implement a pure checked-integer policy-v1 selector with deterministic sorting and nearest-rank p50/p95 calculation over twenty-four samples, where p95 is item 23 and maximum is item 24.
- [x] 6.2 Implement the specified minimum-RTT heuristic, p95 variation, one-millisecond guard, and exact rational `budget × 16,777,216 / (280,896 × 1,000,000)` upward rounding without pre-rounding frame duration.
- [x] 6.3 Raise valid candidates below the negotiated minimum, and reject candidates above the negotiated maximum as `CALIBRATED_TARGET_OUT_OF_RANGE` plus every invalid arithmetic path without clamping.
- [x] 6.4 Add `Auto (Stable)` negotiation with a two-frame minimum.
- [x] 6.5 Include `Auto (Low Latency, Experimental)` with a one-frame minimum in the unpublished exact release candidate so that the prospective artifact itself can be qualified.
- [x] 6.6 Make the negotiated minimum the stricter peer policy and freeze both policy and selected delay for every non-disconnected state.
- [x] 6.7 Make the client recompute and verify the complete-vector digest, p50/p95/max statistics, selector output, supported range, production floor, selection reason, and final readiness value.
- [x] 6.8 Add selector boundary tables covering zero/one-millisecond resolution, exact rational quotient/remainder edges, the excluded single maximum outlier, multiple outliers, asymmetric waits, maximum range, and every overflow.
- [x] 6.9 Add deterministic tests showing the historical contaminated ACCEPT/ACK duration is no longer read by policy v1.

## 7. Integrate deterministic policy with attachment and replicas

- [x] 7.1 Reorder session attachment to validate profiles, calibrate, select delay, accept identities, and only then capture and exchange replicas.
- [x] 7.2 Preserve the quiescent attachment deadline and pause semantics while assigning a separate bounded calibration operation.
- [x] 7.3 Ensure no incompatible, failed, or timed-out profile/calibration path mutates the original core or sends a replica manifest/chunk.
- [x] 7.4 Install normalized RTC and negotiated deterministic configuration identically into both endpoint pairs before topology settlement and guest release.
- [x] 7.5 Include profile, RTC, capability, calibration, selection, and policy identities in periodic verification diagnostics without adding nondeterministic fields to state digests.
- [x] 7.6 Add paired-session tests for success, every profile mismatch, unsupported peripherals, calibration outcomes, replica exchange after calibration, final readiness, and teardown from every new state.
- [x] 7.7 Verify that state save/load, reset, unload, frontend stop, peer detach, and callback invalidation remain correct throughout profile and calibration states.

## 8. Add latency and wait-cause telemetry

- [x] 8.1 Record provisional/calibration identity, vector digest, sample count, minimum, nearest-rank p50, nearest-rank p95, maximum, policy version, production floor, selected delay, and selection reason.
- [x] 8.2 Record per-player input arrival lead in frames and microseconds at authoritative insertion and consumption.
- [x] 8.3 Separately per endpoint, record released/waited frames, wait-free ratio, one aggregate input-wait duration per waited frame, nearest-rank p95, maximum tail, and input deadline misses while excluding verification-only waits.
- [x] 8.4 Record input poll-to-successful-send timing where the frontend/transport timing boundary is observable without changing callback ordering.
- [x] 8.5 Emit bounded attach, periodic, failure, and teardown summaries with calibration packet/byte totals and copied-queue high-water marks.
- [x] 8.6 Add deterministic analyzer tests for per-endpoint ratio/p95/maximum, zero-wait runs, multiple polls aggregated once, isolated input waits, excluded verification waits, one-endpoint tail failure, deadline failures, and privacy exclusions.
- [x] 8.7 Measure whether pending verification materially delays input authoring, keep authoring after verification throughout this change, and record any future reordering opportunity for a separately reviewed specification.

## 9. Prove exact fixed-delay input behavior

- [x] 9.1 Extend input-sync tests to prove `sample F -> logical F+D` exactly once across ring wrap, redundancy, duplicates, and the one- and two-frame policies.
- [x] 9.2 Inject packets just before and after frame readiness to prove late input blocks but never repeats, drops, predicts, or retroactively changes a record.
- [x] 9.3 Compare byte-identical logical input and machine traces across immediate, delayed, jittered, duplicate, and bounded-rendezvous transports.
- [x] 9.4 Test input timeout, queue exhaustion, conflicting duplicate, wrong owner, wrong generation, and synchronous stop with complete checkpoint restoration.
- [x] 9.5 Verify zero-frame ranges and every mid-session delay/policy mutation are rejected.
- [x] 9.6 Run the continuous diagnostic ROM and LinkCable workload under deterministic one-, two-, and higher-frame simulated policies with matching P0/P1 traces.
- [x] 9.7 Confirm the local lockstep cable transaction rate remains independent of input packet timing and no protocol-v1 transfer packet appears.

## 10. Documentation and release tooling

- [x] 10.1 Update the protocol-v2 document with HELLO nonces, provisional calibration identity, exact role/state table, profile fields, player-owned RTC normalization/teardown table, unsupported inputs, vector digest, exact selector arithmetic, policy options, and compatibility version.
- [x] 10.2 Update user documentation with stable versus qualified low-latency behavior, expected added milliseconds, unsupported sensor cartridges, actionable errors, and unchanged host/join workflow.
- [x] 10.3 Update the validation matrix with requirement-to-test mapping, selector vectors, exact baseline/candidate evidence, and whether the shipped production floor is one or two frames.
- [x] 10.4 Extend sampled runtime-log analysis to parse calibration, selected-policy, input-lead, and separated wait-cause metrics.
- [x] 10.5 Extend the private Android qualification manifest/helper to require exact core identity, expected latency policy, selected delay, isolated paths, controllers, and fresh evidence without modifying normal device configuration or automating stock RetroArch host/join through input injection.
- [x] 10.6 Add mocked helper/analyzer tests for wrong policy, wrong selected delay, stale logs, missing metrics, malformed percentiles, and valid stable/low-latency runs.
- [x] 10.7 Document that raw commercial inputs and content remain private and that logs contain no input history, address, save bytes, or paths.

## 11. Automated final gates

- [x] 11.1 Run all focused normal protocol, profile, RTC, capability, calibration, selector, session, replica, input, adapter, and analyzer suites.
- [x] 11.2 Run the focused ASan/UBSan suite with leak detection and fail-fast settings.
- [x] 11.3 Run the focused TSan suite over calibration, callbacks, session, replicated pair, and teardown paths.
- [x] 11.4 Run the complete normal mGBA test suite and compare the pinned upstream baseline exception separately.
- [x] 11.5 Reproduce the CC0 link and RTC fixtures plus analyzer outputs from source with the pinned toolchain.
- [x] 11.6 Build and inspect the Android ARM64 libretro shared object in CI.
- [ ] 11.7 Obtain focused independent review of the deterministic profile, RTC policy, external-input rejection, selector mathematics, session transitions, and test evidence before physical release qualification.

## 12. Exact-device latency qualification

- [ ] 12.1 Build, hash, install, and runtime-identify an unpublished exact reviewed Android ARM64 release candidate containing both stable and low-latency options on the Thor and Odin through the documented automation-owned workflow.
- [ ] 12.2 Run the automation-owned continuous fixture under `Auto (Stable)` for 30 minutes and record FPS, audio, state checks, SIO throughput, calibration, selected delay, input lead, rendezvous, memory, and temperature.
- [ ] 12.3 If the selector produces one frame, run that identical unpublished artifact for at least 1,800 seconds and 106,200 released frames; require separately on both endpoints at least 99% wait-free frames, waited-frame nearest-rank p95 at most 8,000 microseconds, maximum at most 16,743 microseconds, and every existing correctness/performance gate.
- [ ] 12.4 Run a separate deliberately impaired jitter case and prove stable policy selects or negotiates a healthy two-or-more-frame session without changing logical traces; report the same per-endpoint wait-free/p95/maximum metrics but exclude the run from one-frame statistics and thresholds.
- [ ] 12.5 If and only if automation passes, hand the prepared devices to the human tester for stock RetroArch host/join plus one concise Mario Kart lobby/race-entry, representative gameplay, audiovisual, and responsiveness smoke followed by safe disconnect.
- [ ] 12.6 Keep stock RetroArch host/join, commercial navigation, and subjective judgment human-owned; automation may connect only through a separately validated non-input interface and otherwise stops rather than injecting hotkeys or exploring controls/menus.
- [ ] 12.7 If all one-frame gates pass, publish the identical candidate with `Auto (Low Latency, Experimental)`; do not rebuild or change option exposure after qualification.
- [ ] 12.8 Record exact source SHA, binary hash, frontend identity, device roles, selected policy/delay, summarized evidence, and cleanup result without committing ROMs, saves, raw inputs, or private device data.
- [ ] 12.9 If any one-frame gate fails, do not publish that artifact; remove or disable the option, rerun all automated gates, and run a fresh exact-final-artifact stable fixture plus human-owned host/join and brief commercial smoke.
- [ ] 12.10 On the exact artifact selected for publication, perform a brief human-owned Four Swords discovery regression through several verification intervals and clean teardown without repeating the prior long qualification.

## 13. Finalize the experimental alpha increment

- [ ] 13.1 Re-run the complete applicable automated gates against the immutable final head after all documentation/evidence edits and, if option exposure or build configuration changed after physical testing, repeat the exact-final-artifact physical stable fixture and commercial smoke.
- [ ] 13.2 Update README, release notes, compatibility table, build provenance, protocol documentation, and validation matrix to match the actually shipped one- or two-frame floor.
- [ ] 13.3 Confirm the branch contains no generated build trees, private manifests, device logs, ROMs, saves, screenshots, temporary core binaries, or unrelated tooling.
- [ ] 13.4 Reconcile every OpenSpec requirement and task with exact evidence, marking a failed one-frame experiment honestly rather than treating it as required for the two-frame improvement.
- [ ] 13.5 Obtain final independent review, merge the reviewed change, sync its capability specifications, archive it, and tag/publish only the explicitly qualified experimental build.
