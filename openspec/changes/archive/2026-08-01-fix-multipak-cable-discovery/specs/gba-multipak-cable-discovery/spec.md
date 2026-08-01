## ADDED Requirements

### Requirement: Baseline-first conditional investigation
The project SHALL retest Four Swords on the exact topology-settled alpha.2 build before adding common observer infrastructure or changing production behavior. If that build links successfully, this change SHALL add a non-commercial topology regression and exact qualification evidence, SHALL introduce no speculative behavior change, and SHALL treat permanent common observer infrastructure as a separate decision. If the build still fails, the project SHALL proceed with the diagnostic requirements below.

#### Scenario: Topology-settled baseline succeeds
- **WHEN** both alpha.2 endpoints connect before cable discovery and Four Swords enters usable shared gameplay
- **THEN** no additional production behavior correction is implemented under this change
- **AND** the already-correct topology transition receives non-commercial regression coverage and exact-build qualification evidence
- **AND** the common observer is not mandatory for completion

#### Scenario: Topology-settled baseline still fails
- **WHEN** the exact alpha.2 run remains in discovery or reports a link failure within the agreed time box
- **THEN** the compatibility failure is retained
- **AND** Stage A proceeds with bounded diagnostic instrumentation and deterministic layered isolation

### Requirement: Deterministic layered fault isolation
If the baseline-failure scenario activates Stage A, the project SHALL diagnose discovery in widening order through ordinary local mGBA lockstep, a network-free replicated pair, the paired protocol-v2 adapter, and finally physical frontend behavior. Layers one through three SHALL use identical P0 and P1 initial-state payloads, emulator configuration, bounded per-frame P0/P1 input script, and named comparison boundary. Investigation SHALL stop at the first failing layer.

#### Scenario: Automated ladder begins
- **WHEN** a human has created or verified the bounded pre-link navigation once
- **THEN** automation records one private per-frame P0/P1 input script
- **AND** ordinary lockstep, direct replicated pair, and paired protocol-v2 adapters replay that exact script from identical player states and configuration
- **AND** only the script digest and frame range enter the run evidence

#### Scenario: Ordinary local lockstep fails
- **WHEN** the qualification transition fails with two ordinary local mGBA cores and the existing lockstep coordinator
- **THEN** replicated-pair and network code are not modified to compensate
- **AND** the evidence identifies the local SIO or lockstep boundary requiring an exact reviewed invariant or separately approved capability

#### Scenario: Local lockstep passes but replicated pair fails
- **WHEN** ordinary local lockstep reaches the expected discovery outcome but the equivalent network-free replicated pair does not
- **THEN** diagnosis stops at replica restoration, topology settlement, pair scheduling, or local peripheral assignment
- **AND** protocol-v2 behavior remains unchanged

#### Scenario: Both local layers pass but protocol v2 fails
- **WHEN** ordinary lockstep and the network-free replicated pair produce equivalent successful transitions but paired protocol-v2 adapters diverge
- **THEN** diagnosis stops at attachment, bundle installation, input seeding, session release, or adapter integration

#### Scenario: All automated layers agree
- **WHEN** local lockstep, the replicated pair, and paired protocol-v2 adapters produce equivalent successful anchored traces
- **THEN** the exact build advances to prepared two-device qualification

#### Scenario: Discovery uses a supported NORMAL mode
- **WHEN** Four Swords uses NORMAL8 or NORMAL32 during discovery and ordinary local mGBA lockstep already supports that transition
- **THEN** the transition remains within Stage A and is reproduced through the existing local replicated pair
- **AND** no new wire behavior is implied

#### Scenario: Discovery requires an unsupported serial capability
- **WHEN** ordinary local lockstep demonstrates a need for an unimplemented serial mode, electrical model, or distributed behavior
- **THEN** widening stops at that boundary
- **AND** the proposal and capability scope are revised and reviewed before implementation

### Requirement: Guest-observation and attempted-write tracing
If the baseline-failure scenario activates Stage A tracing, the emulator SHALL provide an opt-in transport-neutral observer that records discovery-relevant guest observations, write intent, and state transitions. Identical consecutive SIOCNT or RCNT reads SHALL be coalesced into bounded `REGISTER_READ_RUN` records. Before emitting any non-read SIO, lockstep, pair, adapter, anchor, or terminal transition used in causal comparison, the observer SHALL flush every open read run whose observations precede that transition. Write records SHALL retain attempted value, access width, pre-state, post-state, and transition origin even when masks or role recomputation hide the attempted operation in the final register.

#### Scenario: Guest polls SIOCNT or RCNT
- **WHEN** guest code repeatedly reads the same SIOCNT or RCNT value without an intervening causal boundary
- **THEN** the observer emits a coalesced read run containing register, access width, returned value, first local cycle, last local cycle, first cable cycle and last cable cycle where available, count, logical player, mode, callback ID, participant relationship, and trace phase

#### Scenario: A read run reaches any recorded causal boundary
- **WHEN** a returned-value change, mode change, write, topology publication, driver attachment, ID assignment, queued attach/mode event, START, scheduling, wake/sleep, completion, detach, reset, terminal failure, anchor, or trace termination is about to emit a non-read record
- **THEN** every preceding open read run is emitted with an earlier run ordinal before that non-read record
- **AND** no coalesced run spans both sides of the transition even when its returned value is unchanged

#### Scenario: Completion follows a polling run
- **WHEN** a player repeatedly reads busy SIOCNT and a transfer completion is about to emit its record
- **THEN** the busy read run is flushed before the completion record
- **AND** a later read of cleared SIOCNT begins a new run ordered after completion

#### Scenario: Guest attempts a register write
- **WHEN** guest code writes SIOCNT or RCNT
- **THEN** the observer records the attempted value, access width, pre-state, post-state, operation ID, and `GUEST_WRITE` origin
- **AND** resulting mode, line, and START effects correlate to the same operation ID
- **AND** a preceding read run may reference that operation only through `flush_boundary_operation_id` and retains its earlier run ordinal

#### Scenario: Internal code changes visible SIO state
- **WHEN** replica restoration, topology settlement, completion, detach, or reset changes guest-visible SIO state
- **THEN** the observer records the corresponding explicit origin rather than classifying it as a guest write

#### Scenario: SIOMULTI read evidence is required
- **WHEN** diagnosis is expanded to observe a SIOMULTI data-register read
- **THEN** the trace records register identity and only the redacted classification `FFFF`, `0000`, or `OTHER`
- **AND** it does not record the commercial data word

#### Scenario: Tracing is disabled
- **WHEN** no observer is installed or the diagnostic option is off
- **THEN** SIO, lockstep, replicated-pair, protocol-v2, allocation, formatting, file-output, and network behavior remain unchanged

### Requirement: Correlated event ordering and observer safety
If the baseline-failure scenario activates Stage A tracing, every transition record SHALL be a fully copied immutable value containing a globally monotonic run ordinal, per-player ordinal, coordinator ordinal where applicable, operation ID where applicable, optional flush-boundary operation ID where applicable, transfer ID where applicable, actor player, target player or mask, transition origin, and trace phase. An operation ID SHALL correlate only an attempted register operation with its resulting mode, line, and START effects. If an earlier read run identifies the operation that ended it, the run SHALL use `flush_boundary_operation_id` and SHALL NOT become part of that operation. After setup, event emission SHALL be non-blocking, allocation-free, formatting-free, non-reentrant, safe while the coordinator mutex is held, and safe under ordinary local-lockstep threading.

#### Scenario: One operation has several effects
- **WHEN** an attempted write changes mode or lines and produces a START edge
- **THEN** all resulting records share one operation ID
- **AND** run, player, and coordinator ordinals preserve their observed order

#### Scenario: One transfer crosses both players
- **WHEN** START, scheduling, player wake/sleep, and completion belong to one transfer
- **THEN** those records share one transfer ID
- **AND** actor and target fields identify the affected player relationship

#### Scenario: Event is emitted while coordinator state is locked
- **WHEN** lockstep emits an attachment, ID, mode, waiting, scheduling, wake, or completion transition while holding its coordinator mutex
- **THEN** all required state is copied before the callback
- **AND** the callback does not query live SIO or coordinator state, wait on another emulator thread, or re-enter SIO or lockstep

#### Scenario: Observer cannot accept a record
- **WHEN** the configured sink cannot accept an enabled-path record without blocking
- **THEN** emission increments an observer-loss counter and returns without altering emulation
- **AND** later comparison treats the affected region as incomplete

#### Scenario: Observer owner is destroyed
- **WHEN** content stops, fails, resets, or unloads
- **THEN** the observer is detached before destruction of cores, coordinators, callbacks, frontend state, or ROM storage

### Requirement: Immutable bootstrap and pre-mutation terminal evidence
If the baseline-failure scenario activates Stage A tracing, each enabled endpoint trace SHALL preserve an immutable prefix containing at least the first 128 transition records, a fixed-capacity rolling middle ring, and an immutable terminal snapshot captured immediately before the first teardown, detach, checkpoint restore, or driver-removal mutation. The export SHALL state first emitted ordinal, first retained rolling ordinal, last retained ordinal, total emitted, total overwritten, and total observer losses.

#### Scenario: Discovery runs longer than trace capacity
- **WHEN** emitted transitions exceed rolling capacity
- **THEN** only the oldest rolling-middle records are overwritten
- **AND** the immutable bootstrap prefix and pre-mutation terminal snapshot remain intact
- **AND** overflow cannot pause, fail, or change emulation

#### Scenario: Failure triggers state restoration
- **WHEN** a failing pair is about to detach or restore a verified single-player checkpoint
- **THEN** the terminal snapshot is copied before that first mutation
- **AND** it describes the failing pair rather than the recovered core

#### Scenario: Decisive region is not complete
- **WHEN** ordinal ranges, overwrite counts, or observer-loss counts show that the apparent first divergence is not fully retained
- **THEN** the analyzer rejects the result as inconclusive

### Requirement: Comparable run metadata and causal anchors
If the baseline-failure scenario activates Stage A tracing, every automated trace header SHALL include source commit, core binary hash, observer schema version and capacities, trace-start phase, run ID, layer, endpoint role, ROM identity digest, P0 and P1 initial-state digests, BIOS/HLE policy and BIOS digest where applicable, determinism/core-option profile digest, RTC policy, idle-optimization policy, input-direction policy, and private input-script digest and frame range. Identity digests SHALL be diagnostic metadata only and SHALL NOT select production behavior or expose raw commercial inputs.

Anchors SHALL use these exact emitting conditions:

| Anchor | Normative meaning |
| --- | --- |
| `STATE_ACCEPTED` | Initial P0/P1 states, configuration, content identity, and private-script identity are validated, before attachment mutation. |
| `ATTACHMENT_BEGIN` | Immediately before the first operation that constructs or attaches the compared cable topology. |
| `ATTACHMENT_COMMITTED` | Both players have final IDs, guest-visible topology is materialized, and provisional bootstrap work is settled. |
| `GUEST_RELEASED` | Immediately before either guest executes its first post-commit instruction. |
| `DISCOVERY_INPUT_BEGIN` | First input-script frame designated as beginning cable-discovery interaction. |
| `FIRST_CABLE_OBSERVATION` | First post-release guest read or attempted write of SIOCNT or RCNT, or a later explicitly approved discovery-relevant register. |
| `FIRST_START` | First accepted guest START edge; a rejected attempted START does not qualify. |
| `FIRST_TRANSFER_COMPLETE` | First transfer whose normal completion path has completed on both logical players. |
| `TERMINAL` | After all prior read runs are flushed and immediately before the pre-mutation terminal snapshot. |

Ordinary local lockstep SHALL map attachment anchors around local driver attachment, final IDs, visible topology, settled coordinator work, and the first post-settle guest instruction. A direct replicated pair SHALL use the same meanings around pair construction and topology settlement. Protocol-v2 adapters SHALL map any additional session scaffolding around these anchors without weakening their guest-visible conditions.

#### Scenario: Comparable automated runs are exported
- **WHEN** two layers are prepared for comparison
- **THEN** their initial-state, configuration, content, input-script, schema, capacity, and trace-start metadata match
- **AND** each anchor is present or maps to an explicitly defined equivalent boundary

#### Scenario: Run metadata differs
- **WHEN** initial-state, BIOS/HLE, determinism profile, RTC, idle optimization, input-direction, content, or input-script metadata differs
- **THEN** the analyzer rejects causal equivalence rather than attributing the result to a cable layer

#### Scenario: Architecture-specific setup differs
- **WHEN** ordinary lockstep lacks replica-exchange or pair-commit events present in a replicated layer
- **THEN** the analyzer compares the attachment invariant at `ATTACHMENT_COMMITTED`
- **AND** it begins guest-observation comparison at `GUEST_RELEASED` rather than reporting scaffolding as the first failure

#### Scenario: First attempted START is rejected
- **WHEN** a guest write requests START but common SIO or the active driver rejects that edge
- **THEN** the write remains an attempted-write event
- **AND** `FIRST_START` is not emitted until the first accepted guest START edge

#### Scenario: Terminal anchor is reached
- **WHEN** the trace is about to capture its terminal snapshot
- **THEN** every open read run is emitted first
- **AND** `TERMINAL` is emitted immediately before the pre-mutation snapshot

### Requirement: Relative causal trace comparison
If the baseline-failure scenario activates Stage A, the repository SHALL provide a deterministic comparator that validates metadata and ordinal integrity, compares attachment invariants at `ATTACHMENT_COMMITTED`, compares guest-visible reads and writes from `GUEST_RELEASED`, and compares transfer behavior after `FIRST_START`. It SHALL ignore absolute cycle origin and host wall time but SHALL compare relative causal ordering and characterized timing relationships. Every non-exact timing tolerance SHALL come from a versioned comparison-policy entry containing relationship, units, normalization rule, minimum, maximum, authority or characterization source, and policy version. Deterministic emulated-cycle relationships SHALL use exact equality by default unless an applicable policy explicitly permits a window.

#### Scenario: Equivalent transitions have different origins
- **WHEN** two traces contain equivalent anchored state transitions with different absolute cycle origins or host wall times
- **THEN** the comparator retains those values diagnostically
- **AND** it does not classify origin difference alone as a semantic divergence

#### Scenario: Guest observation changes causal order
- **WHEN** one layer reads a cable state before the transition intended to satisfy it and another reads after that transition
- **THEN** the comparator reports a semantic happens-before divergence at the first affected read or transition

#### Scenario: Relative transition timing differs
- **WHEN** guest-release-to-access, write-to-visible-line, mode-to-ready, START-to-schedule, schedule-to-completion, or P0/P1 cable-time relation differs
- **THEN** the comparator classifies a difference as harmless only when it falls within an applicable authoritative versioned timing policy
- **AND** it classifies changed ordering or an exceeded characterized timing window as a semantic timing divergence

#### Scenario: Timing difference has no characterization policy
- **WHEN** a non-equal relative timing difference has no applicable versioned policy and is not eliminated by the exact deterministic-cycle default
- **THEN** the comparator reports `UNCHARACTERIZED_TIMING_DIFFERENCE`
- **AND** it does not report equivalence or harmless diagnostic scaling

#### Scenario: Comparison evidence is incomplete
- **WHEN** an anchor is missing, a record is malformed, an ordinal has an unexplained gap, metadata is incompatible, or the decisive region was lost
- **THEN** the comparator fails closed as inconclusive
- **AND** it does not report the layers as equivalent

### Requirement: Mandatory evidence-to-requirement review gate
If the baseline-failure scenario activates Stage A, no production behavior change SHALL be implemented until transition evidence has been incorporated into a reviewed requirement defining the affected layer's exact generic invariant. This gate SHALL apply to common SIO, ordinary lockstep, replica capture/restoration, pair installation/scheduling, frontend adapter, session, and wire behavior. The reviewed delta SHALL state pre-state, trigger or observation, required post-state, relative ordering or characterized timing, minimum regression, compatibility effect, and rollback behavior.

#### Scenario: Stage A identifies a causal difference
- **WHEN** the evidence report identifies the first causal state, ordering, or characterized timing divergence
- **THEN** proposal, design, affected capability specification, and tasks are amended with the exact generic invariant
- **AND** focused review is obtained before Stage B begins

#### Scenario: Diagnosis suggests a non-wire correction
- **WHEN** the likely fix concerns SIO lines, role or ready semantics, secondary START, lockstep scheduling, topology settlement, replica restoration, pair release, or frontend attachment timing
- **THEN** the same reviewed-delta gate applies even though no packet changes

#### Scenario: Diagnosis suggests a wire correction
- **WHEN** the likely fix requires a packet, field, protocol version, or distributed ordering change
- **THEN** the reviewed delta additionally defines compatibility, negotiation, versioning, failure handling, and mixed-version behavior

#### Scenario: Current topology settlement is sufficient
- **WHEN** the baseline retest succeeds without new production behavior
- **THEN** no Stage B correction requirement is created
- **AND** only the existing generic topology behavior is captured in regression evidence

### Requirement: Redistributable causal regression
If the baseline-failure scenario activates Stage B after focused review, the approved causal invariant SHALL be represented by an automated regression containing no commercial ROM, save, savestate, screenshot, audio, extracted code, transferred content words, raw private input script, or title identity. It SHALL fail on the prior generic behavior, assert the reviewed immediate invariant, and assert the eventual discovery or transfer consequence.

#### Scenario: Direct register sequence is sufficient
- **WHEN** the approved causal behavior can be expressed through common SIO or lockstep calls
- **THEN** a focused unit test drives the minimum register, read, timing, and coordinator sequence
- **AND** it asserts guest-visible state, role, busy/completion behavior, and queued coordinator work

#### Scenario: Guest instruction ordering is required
- **WHEN** direct calls cannot reproduce the approved causal ordering
- **THEN** the project adds or extends original CC0 guest code
- **AND** its source, reproducible build, binary hash, and expected redacted trace are committed

#### Scenario: Regression suite runs after correction
- **WHEN** focused normal, ASan/UBSan, TSan, complete mGBA, fixture, analyzer, and Android-build gates execute
- **THEN** the new regression and all existing topology, pair, transfer, teardown, and save-checkpoint regressions pass

### Requirement: Explicit physical-test ownership and qualification
Every physical run SHALL assign automation-owned setup/observation and human-owned game interaction before execution. Automation SHALL prepare the exact build, staged-artifact hashes, isolated configuration, synchronized run ID, logging, monitoring, evidence extraction, teardown, and cleanup. When Android prevents ADB from reading RetroArch's app-private installed core, the human SHALL install the exact artifact and preserve Core Information identity evidence; the manifest SHALL record the installed hash as `null` with reason `APP_PRIVATE_PATH_UNREADABLE`, and automation SHALL validate the staged artifact identity, evidence digest, loaded app-private path, and expected runtime protocol registration without inventing an installed hash. The human SHALL perform save selection, navigation, sustained input, gameplay, and audiovisual judgment. Automation SHALL NOT explore unfamiliar menus or gameplay through iterative screenshots and injected input.

#### Scenario: Prepared run reaches handoff
- **WHEN** automation verifies the exact staged artifacts, isolated paths, frontend/content/runtime identity, logging, connection state, expected screen, and failure signals, and validates the human-owned app-private core identity evidence where direct hashing is unavailable
- **THEN** it hands the devices to the human with a short action and success checklist
- **AND** it observes without driving gameplay

#### Scenario: Prepared state differs
- **WHEN** expected frontend, content, save, controller, or connection state is absent
- **THEN** automation stops and reports the mismatch
- **AND** it does not improvise changes to menu drivers, hotkeys, controller mappings, or user configuration

#### Scenario: Four Swords succeeds
- **WHEN** both peers connect before discovery and the human completes normal Multi-Pak navigation
- **THEN** both devices leave discovery, enter the same shared gameplay, accept both players' input, preserve usable animation/audio, and complete multiple matched verification intervals without protocol or SIO failure

#### Scenario: Four Swords still fails
- **WHEN** the exact traced build does not leave discovery within the agreed time box
- **THEN** Four Swords remains a known compatibility failure
- **AND** evidence names the first failing layer, anchor, ordinal, guest observation or transition, relative timing, source commit, binary hash, and trace completeness

#### Scenario: Production behavior changed
- **WHEN** Stage B changes SIO, lockstep, replica, pair, adapter, or session behavior
- **THEN** the exact-head Android build also passes a Mario Kart Multi-Pak smoke through lobby formation, selection, representative race gameplay with real cable traffic, and safe disconnect

#### Scenario: Qualification ends
- **WHEN** the human reports success or failure
- **THEN** automation captures and analyzes both endpoint evidence, performs safe teardown, removes disposable diagnostics and user-owned test copies, restores normal configuration, and turns off unattended OLED displays
