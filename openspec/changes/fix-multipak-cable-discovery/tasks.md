## 1. Baseline retest and branch decision

- [x] 1.1 Create a focused feature branch from the merged alpha.2 baseline and record source commit, core hash, RetroArch version, device identities, and current Four Swords status.
- [x] 1.2 Define ignored local paths for the user-owned Four Swords ROM, saves, savestates, private input scripts, screenshots, and raw logs; verify none can enter source control or release packaging.
- [x] 1.3 Prepare a physical-run manifest containing run ID, exact core hash, initial-state/configuration identities where available, isolated options, expected frontend state, success/failure signals, time box, and human action checklist.
- [ ] 1.4 Automation-stage the exact alpha.2 core and synchronized logging on both devices, hand off one topology-settled Four Swords discovery retest to the human, then capture the outcome, perform safe teardown/cleanup, and preserve the same evidence required by applicable Stage 7 qualification tasks.
- [ ] 1.5 If alpha.2 links, add a non-commercial regression for the already-correct topology transition, record task 1.4 as satisfying this branch's physical qualification, explicitly decide whether observer infrastructure warrants a separate change, and mark conditional tasks 2–7 not applicable without repeating the physical run.
- [ ] 1.6 If alpha.2 still fails, retain the known-failure entry and proceed to Stage A diagnostics without changing emulated behavior.

## 2. Stage A structured observer, conditional on task 1.6

- [ ] 2.1 Define a versioned immutable record schema with run, player, and coordinator ordinals; operation, optional flush-boundary operation, and transfer IDs; actor/target; origin; phase; cycle/frame; mode; registers; roles; participant counts; completion state; and copied coordinator state.
- [ ] 2.2 Add an optional common-SIO observer interface whose disabled path performs no allocation, formatting, file output, or network traffic.
- [ ] 2.3 Instrument guest SIOCNT and RCNT reads as coalesced `REGISTER_READ_RUN` records with first/last local and cable cycles, and flush every open run before any subsequent non-read SIO, lockstep, pair, adapter, anchor, or terminal transition used in causal comparison.
- [ ] 2.4 Record SIOCNT and RCNT attempted write value, access width, pre/post state, and explicit guest/restore/topology/completion/detach/reset origin; correlate only the write and its effects with one operation ID and use an optional `flush_boundary_operation_id` on the preceding read run.
- [ ] 2.5 Instrument replica restore, driver attach, ID assignment, queued attach/mode work, topology settlement, mode change, secondary/primary START, scheduling, wake/sleep, completion, detach, reset, and terminal failure at authoritative transition points.
- [ ] 2.6 Implement an immutable prefix of at least 128 records, fixed rolling middle ring, pre-mutation terminal snapshot, ordinal-range metadata, emitted/overwritten counts, and observer-loss count.
- [ ] 2.7 Ensure emission copies all fields before callback and remains non-blocking, allocation-free, formatting-free, non-reentrant, safe under coordinator lock, and safe under ordinary local-lockstep threading.
- [ ] 2.8 Detach observers before destroying cores, coordinators, callbacks, frontend state, or ROM storage on every stop, failure, reset, and unload path.
- [ ] 2.9 Add tests for read-run coalescing and every generic boundary flush, including busy polling ordered before completion and a new cleared-value run after completion; also test attempted-write preservation, operation/flush-boundary/transfer correlation, cross-player ordering, immutable prefix, pre-mutation terminal capture, overflow/loss, concurrency, disabled transparency, and teardown.
- [ ] 2.10 If SIOMULTI reads prove necessary, record only register identity and redacted `FFFF`, `0000`, or `OTHER` classification and test that raw data words never enter traces.

## 3. Stage A deterministic replay and comparison tooling

- [ ] 3.1 Add trace headers containing source/core hashes, schema/capacity/start phase, run/layer/role, diagnostic-only ROM identity digest, both initial-state digests, BIOS/HLE and BIOS digest, determinism profile, RTC, idle optimization, input-direction policy, and private input-script digest/frame range; prove no digest selects production behavior.
- [ ] 3.2 Add a libretro diagnostic option that defaults off, can be preconfigured without menu interaction, remains local, and cannot affect emulated or negotiated state.
- [ ] 3.3 Add observer/export adapters for ordinary local lockstep, direct `GBAReplicatedPair`, paired protocol-v2 replay, and physical libretro runs using the same schema.
- [ ] 3.4 Add local tooling to record one bounded private per-frame P0/P1 navigation script and replay it from exact P0/P1 states and configuration through automated layers one through three.
- [ ] 3.5 Emit and validate every anchor at its normative condition, including final visible topology and settled work for `ATTACHMENT_COMMITTED`, pre-instruction `GUEST_RELEASED`, first post-release SIO read/write for `FIRST_CABLE_OBSERVATION`, first accepted edge for `FIRST_START`, bilateral normal completion, and read-flushed pre-snapshot `TERMINAL`; implement documented ordinary-lockstep and direct-pair equivalents.
- [ ] 3.6 Add a comparator that checks attachment invariants at `ATTACHMENT_COMMITTED`, guest observations/writes from `GUEST_RELEASED`, and transfer behavior after `FIRST_START` instead of flattening architecture-specific setup.
- [ ] 3.7 Add a versioned timing-policy table whose entries define relationship, units, normalization, minimum, maximum, authority/source, and policy version; require exact deterministic emulated-cycle equality by default and classify unruled non-equal timing as `UNCHARACTERIZED_TIMING_DIFFERENCE`.
- [ ] 3.8 Fail comparison closed on metadata mismatch, malformed records, missing anchors/snapshots, unexplained ordinal gaps, observer loss, or overwritten decisive regions.
- [ ] 3.9 Add analyzer self-tests for deterministic equivalence, architectural scaffolding differences, read-before-publication divergence, completion-flushed polling order, attempted-write differences, authorized timing windows, uncharacterized timing, semantic timing failure, and every incomplete-evidence path.
- [ ] 3.10 Document trace activation, private input/state policy, export locations, comparison commands, causal classifications, and cleanup.

## 4. Stage A strict ladder and mandatory review gate

- [ ] 4.1 Prepare identical P0/P1 initial-state payloads, one emulator configuration profile, one bounded private input script, their digests, and one named comparison boundary.
- [ ] 4.2 Replay the script through two ordinary local mGBA cores with existing lockstep and capture discovery through first successful transfer or time-boxed failure.
- [ ] 4.3 Replay the same states/configuration/script through a network-free `GBAReplicatedPair` and compare anchored causal evidence with ordinary lockstep.
- [ ] 4.4 Replay the same states/configuration/script through paired protocol-v2 adapters and compare with the successful lower layer.
- [ ] 4.5 Advance to physical Android/frontend comparison only after automated layers agree, unless task 1.4 already supplies the decisive device evidence.
- [ ] 4.6 Publish the Stage A evidence report naming the first failing layer, anchor, run/player/coordinator ordinal, operation/transfer relation, guest observation or attempted write, relative timing, trace completeness, and causal hypothesis.
- [ ] 4.7 Classify a discovery-time NORMAL8/NORMAL32 sequence as in-scope only when ordinary local lockstep already supports it and the existing local pair reproduces it without wire expansion; otherwise revise scope before implementation.
- [ ] 4.8 Convert the diagnosed causal transition into an exact delta requirement for the affected SIO, lockstep, replica, pair, adapter, session, or wire capability, including pre-state, trigger/observation, required post-state, relative ordering or timing, regression, compatibility, and rollback.
- [ ] 4.9 Amend proposal, design, specification, and remaining tasks with that exact invariant and obtain focused review explicitly approving Stage B before changing any production behavior.
- [ ] 4.10 If packets or wire ordering are implicated, additionally define versioning, negotiation, failure semantics, and mixed-version behavior before Stage B approval.

## 5. Stage B reviewed correction and causal regression — blocked until task 4.9

- [ ] 5.1 Implement only the exact generic correction approved by task 4.9; do not infer broader authority from this original diagnostic package.
- [ ] 5.2 Add a non-commercial regression that fails on the prior generic behavior and asserts the reviewed pre-state, trigger/read, post-state, ordering/timing, and eventual discovery or transfer consequence.
- [ ] 5.3 Use direct common-SIO/lockstep calls when sufficient; otherwise add original CC0 guest code with reproducible source, build, binary hash, and redacted expected trace.
- [ ] 5.4 Add layer-specific regressions proving the correction is visible in each affected ordinary-lockstep, direct-pair, protocol-v2, or adapter path.
- [ ] 5.5 Add negative tests proving no ROM identity, title name, captured save/state, raw private input, commercial data word, or fabricated game data influences the correction.
- [ ] 5.6 Re-run the deterministic ladder and prove the reviewed first divergence is removed without introducing a later unexplained divergence.

## 6. Automated regression and performance gates after Stage B

- [ ] 6.1 Run all focused tests normally and record exact commands and results.
- [ ] 6.2 Run the focused suite under ASan/UBSan with leak detection and under TSan with fail-fast settings.
- [ ] 6.3 Run the complete normal mGBA suite and compare any failure with the pinned upstream baseline.
- [ ] 6.4 Reproduce committed CC0 fixtures and run analyzer self-tests plus deterministic paired-adapter replay.
- [ ] 6.5 Run continuous-link localhost qualification and verify matched replicas, normal audio, frame-scaled traffic, no SIO errors, and no trace-induced divergence.
- [ ] 6.6 Measure trace-disabled performance and memory against alpha.2; verify no file output and no material frame-time or allocation regression when disabled.
- [ ] 6.7 Exercise tracing enabled under a polling-heavy synthetic workload and verify capacity, coalescing, loss accounting, and no deadlock or sanitizer/race failure.
- [ ] 6.8 Build and inspect the Android ARM64 core in CI and record exact source commit, ELF properties, size, and SHA-256 hash.

## 7. Exact-head physical qualification after Stage B — baseline-success path is satisfied by tasks 1.4–1.5

- [ ] 7.1 Automation-install the exact candidate on both devices, prepare isolated trace/log configuration, preserve normal controller profiles, and confirm the expected pre-handoff screen.
- [ ] 7.2 Hand the devices to the human for save selection, normal Multi-Pak navigation, brief shared gameplay, dual-player input, and audiovisual judgment.
- [ ] 7.3 Monitor without driving gameplay, then capture both endpoint logs and compare attachment, reads/writes, discovery, transfer, verification, timeout, ordinal, and overflow/loss evidence.
- [ ] 7.4 For Four Swords success, verify both devices leave discovery, enter one session, accept both players' input, maintain usable animation/audio, and complete multiple matched verification intervals without protocol or SIO failure.
- [ ] 7.5 For failure, retain the known-failure entry, publish the first isolated causal boundary, and return to the relevant diagnostic or reviewed-delta task rather than marking qualification successful.
- [ ] 7.6 Automation-stop content, remove disposable diagnostic cores/configurations/logs and user-owned copies, restore normal device state, and turn off unattended OLED displays.
- [ ] 7.7 If Stage B changed SIO, lockstep, replica, pair, adapter, or session behavior, run an exact-head Mario Kart Multi-Pak smoke through lobby formation, character/course selection, representative race gameplay with real cable traffic, and safe disconnect.

## 8. Documentation and final review

- [ ] 8.1 Update compatibility, validation, protocol/runtime, trace-schema, and provenance documentation with the diagnosis, reviewed invariant, correction or no-change result, regression mapping, hashes, and physical outcome.
- [ ] 8.2 Document observer defaults, concurrency contract, retention ranges, causal comparator semantics, configuration/state/input digests, and privacy/content exclusions.
- [ ] 8.3 Reconcile every conditional/not-applicable task with evidence and do not count a discovery-screen dwell as successful qualification.
- [ ] 8.4 Run strict OpenSpec validation and the exact-head CI matrix.
- [ ] 8.5 Present diagnostic infrastructure, Stage A evidence, reviewed invariant, Stage B correction, regression, and documentation as independently reviewable commits.
- [ ] 8.6 Request final focused independent review before merging or publishing a replacement alpha.
