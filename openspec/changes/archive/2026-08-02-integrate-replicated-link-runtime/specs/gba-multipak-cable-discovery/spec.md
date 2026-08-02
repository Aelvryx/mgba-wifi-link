## MODIFIED Requirements

### Requirement: Deterministic layered fault isolation
If the baseline-failure scenario activates Stage A, the project SHALL diagnose discovery in widening order through ordinary local mGBA lockstep, a network-free replicated pair, the paired GBA Wi-Fi Link adapter, and finally physical frontend behavior. Layers one through three SHALL use identical P0 and P1 initial-state payloads, emulator configuration, bounded per-frame P0/P1 input script, and named comparison boundary. Investigation SHALL stop at the first failing layer.

#### Scenario: Automated ladder begins
- **WHEN** a human has created or verified the bounded pre-link navigation once
- **THEN** automation records one private per-frame P0/P1 input script
- **AND** ordinary lockstep, direct replicated pair, and paired GBA Wi-Fi Link adapters replay that exact script from identical player states and configuration
- **AND** only the script digest and frame range enter the run evidence

#### Scenario: Ordinary local lockstep fails
- **WHEN** the qualification transition fails with two ordinary local mGBA cores and the existing lockstep coordinator
- **THEN** replicated-pair and network code are not modified to compensate
- **AND** the evidence identifies the local SIO or lockstep boundary requiring an exact reviewed invariant or separately approved capability

#### Scenario: Local lockstep passes but replicated pair fails
- **WHEN** ordinary local lockstep reaches the expected discovery outcome but the equivalent network-free replicated pair does not
- **THEN** diagnosis stops at replica restoration, topology settlement, pair scheduling, or local peripheral assignment
- **AND** GBA Wi-Fi Link adapter behavior remains unchanged

#### Scenario: Both local layers pass but the paired adapter fails
- **WHEN** ordinary lockstep and the network-free replicated pair produce equivalent successful transitions but paired GBA Wi-Fi Link adapters diverge
- **THEN** diagnosis stops at attachment, bundle installation, input seeding, session release, or adapter integration

#### Scenario: All automated layers agree
- **WHEN** local lockstep, the replicated pair, and paired GBA Wi-Fi Link adapters produce equivalent successful anchored traces
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
- **THEN** SIO, lockstep, replicated-pair, GBA Wi-Fi Link adapter, allocation, formatting, file-output, and network behavior remain unchanged

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

Ordinary local lockstep SHALL map attachment anchors around local driver attachment, final IDs, visible topology, settled coordinator work, and the first post-settle guest instruction. A direct replicated pair SHALL use the same meanings around pair construction and topology settlement. Paired GBA Wi-Fi Link adapters SHALL map any additional session scaffolding around these anchors without weakening their guest-visible conditions.

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
