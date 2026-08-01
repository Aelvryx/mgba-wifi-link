## Context

Protocol v2 runs an identical two-player mGBA lockstep pair on each endpoint and synchronizes frame inputs rather than individual cable words. Diagnostic fixtures, Mario Kart, and Advance Wars demonstrate full-speed generic MULTI traffic. Four Swords previously reached cable discovery under protocol v2 but emitted no observed MULTI transfers. The replicated-pair implementation has since gained explicit guest-visible topology settlement, but the title has not been rerun on that exact correction.

The uncertainty is concentrated before the normal transfer stream. A discovery loop may depend on what guest code reads, on an attempted write whose intent is masked in the final register, or on the relative order between a guest observation and topology publication. A trace of state changes alone can therefore appear identical while omitting the cause.

Raw commercial ROMs, saves, savestates, private input scripts, screenshots, and extracted content are user-owned diagnostic inputs and SHALL remain outside the repository and published evidence. Approved cryptographic identity digests may be retained in manifests and evidence without exposing their inputs. The established physical-test ownership policy applies: automation prepares and observes runs; a human performs navigation and gameplay.

## Goals / Non-Goals

**Goals:**

- Retest the topology-settled alpha.2 build before adding permanent infrastructure.
- If the failure remains, identify the first causal layer and exact guest-visible transition using deterministic initial states, configuration, input replay, structured observations, and happens-before comparison.
- Produce a reviewed exact generic invariant before any production behavior change.
- Preserve the replicated-pair architecture and full-speed frame-scaled network behavior.
- After focused Stage B approval, convert the diagnosed cause into a redistributable regression and qualify the correction physically.

**Non-Goals:**

- Pre-authorizing an unknown change to common SIO, lockstep, replica, pair, adapter, or wire behavior.
- ROM-hash-driven production behavior, title detection, compatibility shims, game-specific timing, fabricated cable data, or Four Swords protocol emulation. Diagnostic manifests may contain approved identity digests.
- Four-player, Single-Pak, RFU, sensor/RTC synchronization, compatibility groups, internet transport, or protocol stabilization.
- Committing or publishing commercial ROMs, saves, savestates, raw input scripts, screenshots, audio, or extracted game data.
- Driving unfamiliar RetroArch or game menus through iterative screenshots, taps, or key injection.

## Decisions

### 1. Retest the topology-settled release before building the observer

The first physical action is one time-boxed Four Swords discovery attempt using the exact alpha.2 core, connecting before entering the cable menu. Automation prepares isolated logging and preserves device configuration; the human navigates the title.

If linking succeeds, this change adds the non-commercial topology regression and records exact-head qualification. The common observer and comparison infrastructure then become a separate value decision and are not mandatory merely because they appeared in the failure contingency plan. No production behavior change is invented.

If linking still fails, Stage A proceeds with the observer and diagnostic ladder below.

Alternative considered: instrument first. Rejected because the strongest prior hypothesis has already been corrected and may have resolved the failure.

### 2. Stage A diagnosis cannot authorize Stage B behavior

Tasks through the evidence report are diagnostic only. After the first causal difference is identified, the planning artifacts must be amended to state:

- the affected existing capability and code layer;
- the observed pre-state;
- the triggering event or guest observation;
- the required post-state;
- the relevant happens-before or characterized timing relationship;
- the minimum non-commercial regression;
- compatibility and rollback consequences.

Focused review of that exact delta is required before any production behavior change. This gate applies equally to common SIO, ordinary lockstep, replica capture/restoration, pair installation/scheduling, the libretro adapter, session behavior, and packet/wire behavior. A packet or wire change additionally requires explicit version and compatibility review.

Alternative considered: allow the implementer to choose the “smallest generic correction” after diagnosis. Rejected because generic scope does not make an unknown hardware-visible transition reviewable.

### 3. Discovery-time NORMAL modes are conditionally within the investigation

A Four Swords discovery-time NORMAL8 or NORMAL32 transition is within Stage A when ordinary local mGBA lockstep already supports it and the existing local replicated pair can reproduce it without a wire change. Correcting an existing generic transition still requires the Stage B delta review.

Implementing a new serial mode, electrical model, or distributed protocol is a separate capability and requires a proposal revision before code changes. A brief locally supported probe is not declared out of scope merely because normal gameplay later uses MULTI.

### 4. Automated layers use identical deterministic inputs

Layers one through three use:

- the same exact P0 and P1 initial-state payloads and their digests;
- the same emulator/BIOS/HLE, RTC, idle-optimization, input-direction, and relevant core-option profile;
- the same bounded per-frame P0/P1 input script;
- the same named comparison boundary.

The human creates or verifies the short pre-link navigation once. Automation records a bounded private per-frame input script and replays it through ordinary local lockstep, direct `GBAReplicatedPair`, and paired protocol-v2 adapters. The script remains local; only its cryptographic digest and frame range enter the manifest and trace header.

Equivalent menu intentions or two separately timed human attempts are not accepted as deterministic cross-layer control. The final physical Android qualification remains human-driven.

### 5. Traces use explicit causal phases

Every run marks anchors with these exact meanings:

| Anchor | Required emitting condition |
| --- | --- |
| `STATE_ACCEPTED` | P0/P1 initial states, emulator configuration, content identity, and private input-script identity have been validated, before any attachment mutation. |
| `ATTACHMENT_BEGIN` | Immediately before the first operation that constructs or attaches the compared cable topology. |
| `ATTACHMENT_COMMITTED` | Both players have final IDs, guest-visible SIOCNT/RCNT topology is materialized, and provisional bootstrap attachment/mode work is settled. |
| `GUEST_RELEASED` | Immediately before either guest executes its first post-commit instruction. |
| `DISCOVERY_INPUT_BEGIN` | At the first input-script frame explicitly designated as beginning cable-discovery interaction. |
| `FIRST_CABLE_OBSERVATION` | At the first post-release guest read or attempted write of SIOCNT or RCNT, or a later explicitly approved discovery-relevant register. |
| `FIRST_START` | At the first accepted guest START edge; an attempted write whose START is rejected does not satisfy the anchor. |
| `FIRST_TRANSFER_COMPLETE` | After the first transfer's normal completion path has completed on both logical players. |
| `TERMINAL` | After all preceding open read runs are flushed and immediately before the pre-mutation terminal snapshot. |

Layer-specific scaffolding before `GUEST_RELEASED` is not compared as one flat event stream. Comparison first checks attachment invariants at `ATTACHMENT_COMMITTED`, then guest-visible observations and writes from `GUEST_RELEASED`, then transfer behavior after `FIRST_START`.

For ordinary local lockstep, `STATE_ACCEPTED` means both restored cores, configuration, content, and input script are validated; `ATTACHMENT_BEGIN` precedes the first local driver attachment; `ATTACHMENT_COMMITTED` follows final IDs, visible topology, and settled coordinator work; and `GUEST_RELEASED` precedes the first post-settle guest instruction. A direct replicated pair uses the same meanings around pair construction and local topology settlement. Paired protocol-v2 adapters add network/session scaffolding before these anchors, but may not weaken their guest-visible conditions. An anchor that does not otherwise apply maps to a documented equivalent boundary; it is not silently omitted.

### 6. The observer records reads, attempted writes, and their origins

If Stage A tracing is required, common SIO and lockstep code expose an optional observer interface rather than writing frontend log strings directly.

Discovery-relevant guest reads produce coalesced `REGISTER_READ_RUN` records containing register, access width, returned value, first and last local cycles, first and last cable cycles where available, count, logical player, mode, callback ID, participant relationship, and causal phase. Identical consecutive reads coalesce.

Before emitting any non-read SIO, lockstep, pair, adapter, anchor, or terminal transition that participates in causal comparison, the observer flushes every open read run whose observations precede that transition. This generic rule includes returned-value changes, mode changes, writes, topology materialization, driver attachment, ID assignment, queued attach/mode work, START, scheduling, wake/sleep, completion, detach, reset, terminal failure, anchors, and trace termination. A run cannot span both sides of a recorded causal transition even when its returned value is unchanged.

SIOCNT and RCNT reads are always eligible. If later evidence requires SIOMULTI data-register observations, records contain only register identity and a redacted `FFFF`, `0000`, or `OTHER` classification.

Write records contain attempted value, access width, pre-state, post-state, and origin: `GUEST_WRITE`, `REPLICA_RESTORE`, `TOPOLOGY_SETTLEMENT`, `COMPLETION`, `DETACH`, or `RESET`. This preserves guest intent even when writable masks or role recomputation hide it in the final register. `operation_id` belongs only to the attempted write and its resulting effects. A read run flushed immediately before that operation may carry an optional `flush_boundary_operation_id` explaining why it ended, but the preceding observations do not become part of the operation.

Other events include replica restore, driver attach, player-ID assignment, topology settle, mode change, secondary START, primary START, transfer schedule, transfer completion, detach, reset, and terminal failure.

The trace excludes ROM bytes, save bytes, raw transferred words, screen/audio content, and raw input history.

### 7. Event identity and observer concurrency are explicit

Each immutable record contains:

```text
run_ordinal
player_ordinal
coordinator_ordinal where applicable
operation_id where applicable
transfer_id where applicable
actor_player
target_player_or_mask
transition_origin
trace_phase
```

`run_ordinal` is globally monotonic within one endpoint trace run. `player_ordinal` is monotonic per logical core. `coordinator_ordinal` is assigned while the coordinator transition is serialized. `operation_id` correlates an attempted register operation with its resulting mode, line, and START effects; an optional `flush_boundary_operation_id` on the immediately preceding read run identifies the boundary without changing the observations' earlier ordinal. `transfer_id` correlates START, schedule, and completion.

The emitting code fully copies all SIO and coordinator fields into the record before invoking the observer. The callback never queries live SIO or coordinator state. After observer setup, event emission is non-blocking, allocation-free, formatting-free, unable to re-enter SIO or lockstep, safe while the coordinator mutex is held, and safe under the threading model used by ordinary local lockstep. A callback that cannot accept a record increments a loss counter and returns; it cannot wait on another emulator thread.

With no observer installed, execution performs no allocation, formatting, file I/O, or network traffic.

### 8. Retention preserves bootstrap causality and pre-mutation failure state

Each endpoint trace retains:

- an immutable bootstrap prefix of at least the first 128 transition records;
- a fixed-capacity rolling middle ring;
- an immutable terminal snapshot captured immediately before the first teardown, detach, checkpoint restore, or driver-removal mutation;
- total emitted, total overwritten, and observer-loss counters.

The bootstrap prefix preserves replica restoration, provisional and final IDs, queued attach/mode events, topology settlement, and guest release. Ring overflow never overwrites that prefix. Exported ordinal ranges include first emitted ordinal, first retained rolling ordinal, last retained ordinal, total emitted, total overwritten, and total observer losses.

Overflow or observer loss cannot alter emulation. The analyzer refuses to claim equivalence when the apparent divergence lies outside a provably complete retained region.

### 9. Trace headers prove comparable state and configuration

Each export contains:

- source commit and core binary hash;
- observer schema version, capacities, loss counts, and trace-start phase;
- run ID, layer, endpoint role, and approved ROM identity digest;
- P0 and P1 initial-state payload digests;
- BIOS/HLE policy and BIOS digest where applicable;
- determinism/core-option profile digest;
- RTC policy;
- idle-optimization policy;
- input-direction policy;
- private input-script digest and frame range.

This metadata is diagnostic and local; it does not freeze or change the protocol-v2 wire format.

### 10. Comparison is anchored and relative-time aware

The repository tool validates metadata and ordinal integrity before comparison. It compares attachment invariants at `ATTACHMENT_COMMITTED`, guest-visible reads and writes from `GUEST_RELEASED`, and transfer behavior after `FIRST_START`.

Absolute cycle origins and host wall time are irrelevant. The analyzer does compare:

- guest-release to first-SIO-access delta;
- attempted-write to visible-line delta;
- mode-write to ready delta;
- START to schedule delta;
- schedule to completion delta;
- P0/P1 cable-time relationships;
- whether a guest read occurs before or after the state intended to satisfy it.

Every accepted timing tolerance comes from a versioned comparison-policy entry containing relationship, units, normalization rule, minimum, maximum, authority or characterization source, and policy version. Deterministic emulated-cycle relationships use exact equality by default unless such a policy explicitly permits a window. A non-equal timing relationship with no applicable policy is `UNCHARACTERIZED_TIMING_DIFFERENCE`; it is neither equivalent nor harmless.

Results distinguish:

- absolute-origin difference: ignored;
- equivalent transitions within an authoritative versioned timing policy: diagnostic only;
- non-equal timing without an applicable policy: `UNCHARACTERIZED_TIMING_DIFFERENCE`;
- changed happens-before relationship: semantic divergence;
- preserved order exceeding a characterized timing window: semantic timing divergence.

The tool reports the first semantic divergence with bounded context. Missing anchors, mismatched state/configuration/input digests, malformed records, ordinal gaps, observer loss, or overwritten decisive regions fail closed as inconclusive.

### 11. The strict widening ladder stops at the first failure

The deterministic run executes in order:

1. two ordinary local GBA cores using existing lockstep;
2. a network-free `GBAReplicatedPair` from the same player states;
3. paired protocol-v2 adapters using the same input script;
4. physical Android/frontend behavior only after the first three agree.

Investigation stops at the first failing layer. The evidence report is the final Stage A deliverable. No correction begins until Decision 2's artifact amendment and focused review are complete.

### 12. Regression and physical qualification follow the reviewed correction

After Stage B approval, the minimum causal sequence is reproduced through direct SIO/lockstep calls, redistributable pair replay, or original CC0 guest code. It must fail on the pre-correction behavior and assert both the immediate reviewed invariant and eventual discovery/transfer consequence.

Automation owns exact builds, hashes, installation, isolated options, run IDs, logging, deterministic replay, monitoring, evidence extraction, teardown, and cleanup. It may execute only a short verified frontend setup sequence. The human owns save selection, menus, sustained input, gameplay, and audiovisual judgment.

If production SIO, lockstep, pair, replica, adapter, or session behavior changes, an exact-head Mario Kart smoke must reach successful lobby formation, selection, representative race gameplay with real cable traffic, and safe disconnect. A complete three-lap race is not required.

Four Swords success requires both devices to leave discovery, enter shared gameplay, accept both players' input, maintain usable animation/audio, and complete multiple verification intervals without timeout or divergence.

## Risks / Trade-offs

- **Read polling produces excessive events** → Coalesce identical reads and flush only at causal boundaries.
- **Instrumentation changes timing or deadlocks under lockstep** → Copy records before callback; keep the callback non-blocking, allocation-free, formatting-free, and non-reentrant; test enabled and disabled paths under TSan.
- **Human actions differ across layers** → Record one private bounded input script and replay identical frames from identical state/configuration.
- **Architectural setup creates a false first divergence** → Compare named anchors and phase-specific invariants rather than one flat stream.
- **The decisive bootstrap sequence is overwritten** → Preserve an immutable prefix plus a pre-mutation terminal snapshot and fail closed on incomplete regions.
- **Current alpha.2 already fixes Four Swords** → Finish with topology regression and exact qualification; decide observer infrastructure separately.
- **Local lockstep lacks required hardware behavior** → Stop widening and revise scope before implementing a new mode or electrical model.
- **A generic fix regresses proven games** → Run automated gates and the conditional exact-head Mario Kart physical smoke.
- **Human testing becomes slow hybrid automation** → Enforce the two-bucket ownership policy and use a local pre-link save plus concise handoff.

## Migration Plan

1. Retest alpha.2 with no production or observer change.
2. If it passes, add topology regression and exact qualification evidence; stop unless a separate observer decision is approved.
3. If it fails, implement Stage A observer, export, analyzer, deterministic input replay, and self-tests without changing emulated behavior.
4. Run the strict ladder and publish the first causal difference as an evidence report.
5. Amend proposal, design, affected capability requirement, and tasks with the exact generic invariant; obtain focused review.
6. Only after approval, implement Stage B correction and non-commercial regression.
7. Run normal, sanitizer, TSan, complete-suite, fixture, analyzer, Android-build, continuous-link, and conditional Mario Kart gates.
8. Perform exact-build Four Swords qualification and update compatibility evidence.

Observer infrastructure and any later correction are independently revertible. Trace files, commercial states, and private input scripts are disposable and excluded from source control.

## Open Questions

- Does Four Swords now link on alpha.2 after topology settlement?
- At which anchor and layer does the first causal divergence occur?
- Is the decisive behavior a read-before-publication ordering, attempted write intent, NORMAL-mode probe, already-MULTI role observation, secondary START wait, or another generic transition?
- What exact affected capability and invariant must be added at the Stage B review gate?
- Can the eventual regression use direct register calls, or is original guest code required?
