## ADDED Requirements

### Requirement: GBA MULTI Multi-Pak scope
The network SIO driver SHALL emulate two-player Multi-Pak link-cable operation only in GBA MULTI serial mode. It SHALL not emulate Single-Pak multiboot, Wireless Adapter/RFU, NORMAL8, NORMAL32, UART, GPIO, or Joy Bus communication in the MVP.

#### Scenario: Both players commit MULTI mode
- **WHEN** both emulated GBAs have committed GBA MULTI serial mode in an attached session
- **THEN** the network driver enables two-player Multi-Pak transfer coordination

#### Scenario: A player selects an unsupported serial mode
- **WHEN** either emulated GBA selects a serial mode outside GBA MULTI
- **THEN** the network driver does not fabricate a network transfer for that mode
- **AND** common SIO behavior follows the no-network-driver path

### Requirement: Common SIO mode-gated dispatch
Common GBA SIO code SHALL call a driver's new-I/O mode-specific hooks only when the driver exists and `handlesMode()` accepts the current mode. This gate SHALL cover transfer start, connected-device count, device ID where mode-visible, SIOCNT/RCNT hooks, and mode-specific register hooks. When common code schedules a transfer completion, it SHALL latch that transfer's mode; finish dispatch SHALL use the latched completion mode and call its finish hook only when the driver handles that latched mode. A later mode write SHALL not reroute an already scheduled MULTI completion through another mode's finish path. Mode-change lifecycle notification MAY still inform the driver that the local GBA entered or left MULTI.

#### Scenario: NORMAL8 starts while the network driver is attached
- **WHEN** GBA software starts a NORMAL8 transfer while `GBASIONetplayDriver` handles only MULTI
- **THEN** the network driver's start, connected-device, and finish hooks are not invoked
- **AND** registers, timing, completion, and IRQ behavior match an identical core with no network driver

#### Scenario: NORMAL32 starts while the network driver is attached
- **WHEN** GBA software starts a NORMAL32 transfer while `GBASIONetplayDriver` handles only MULTI
- **THEN** the network driver's start, connected-device, and finish hooks are not invoked
- **AND** registers, timing, completion, and IRQ behavior match an identical core with no network driver

#### Scenario: Unsupported mode register hooks are reached
- **WHEN** software writes SIOCNT, RCNT, or mode-specific registers in UART, GPIO, Joy Bus, NORMAL8, or NORMAL32
- **THEN** the network driver's mode-specific register hooks are not called
- **AND** behavior matches the corresponding no-driver path

#### Scenario: MULTI mode is active
- **WHEN** the attached network driver accepts the active MULTI mode
- **THEN** common SIO code may call its MULTI-specific hooks

#### Scenario: Mode changes after a MULTI completion is scheduled
- **WHEN** an active network MULTI transfer has a scheduled completion and the current local SIO mode later changes
- **THEN** new I/O in the new mode does not reach MULTI network hooks
- **AND** the existing completion dispatches through the latched MULTI completion context exactly once

### Requirement: Hardware-consistent attached player state
After atomic session attachment, player zero SHALL be assigned primary/master device ID zero and player one secondary/slave device ID one. The session SHALL track `topological_peer_count = 1` independently from the SIO-level `effective_transfer_peer_count`. Topology SHALL describe acknowledged attachment only and SHALL not select line state or transfer duration. Effective count SHALL be zero while joint MULTI readiness is uncommitted and one while the cable is jointly ready. Common start dispatch SHALL carry an explicit effective count: zero for a no-peer/pre-START path and one for every emitted START, including abort-pending transactions. Unused player slots two and three SHALL remain unattached.

#### Scenario: Attached session is queried before mode commit
- **WHEN** the session is attached but both MULTI modes are not committed
- **THEN** session state reports one topological peer while SIO reports zero effective participants
- **AND** SIOCNT ready is zero, slave is one, and ID is zero

#### Scenario: Ready two-player link is queried
- **WHEN** both peers have committed MULTI mode
- **THEN** player zero reads ID zero and primary status
- **AND** player one reads ID one and secondary status
- **AND** both read SIOCNT ready one
- **AND** SIO reports one effective participant

#### Scenario: Detached pulled-up lines are queried
- **WHEN** no network session is observably attached
- **THEN** SIOCNT ready is one, slave is one, and ID is zero
- **AND** software distinguishes this from joint readiness using the role lines rather than the ready bit alone

#### Scenario: Not-ready start is scheduled
- **WHEN** player zero writes start before joint readiness
- **THEN** the common start result carries effective count zero
- **AND** topological peer count remains one even though ordinary no-peer timing is selected

#### Scenario: Emitted START later aborts
- **WHEN** START was emitted and the transaction becomes abort-pending before host common scheduling
- **THEN** the common start result carries effective count one
- **AND** common scheduling selects the immutable announced two-device duration without mutating topological state

### Requirement: Separate scheduler, grant, and barrier timing
The driver SHALL keep the local scheduler quantum, network execution-grant cadence, and hard-barrier cadence as distinct runtime policies. Local scheduler checks SHALL send no packets. Candidate network horizons SHALL initially be frame-oriented and traceable, but grant exchange SHALL remain host-leading and single-flight. Hard barriers SHALL occur for attachment, mode transition, transfer start, and transfer completion; no periodic hard barrier SHALL be enabled by default.

#### Scenario: Local scheduler event fires
- **WHEN** a local driver timing event checks queued cable work
- **THEN** no packet is sent solely because the local scheduler quantum elapsed

#### Scenario: Network execution grant is issued
- **WHEN** player zero has already executed to the next candidate cable horizon without encountering an earlier host cable event
- **THEN** it sends one grant with a grant sequence and cable-cycle horizon
- **AND** the cadence is independent of mGBA's local `LOCKSTEP_INTERVAL`

#### Scenario: Optional health barrier is configured
- **WHEN** measurement enables a periodic health barrier
- **THEN** its cadence is runtime policy reported in traces
- **AND** changing that cadence does not change wire-protocol compatibility

### Requirement: Bidirectionally safe execution grants
Player zero SHALL own a monotonic 64-bit virtual cable clock and lead execution. From the last acknowledged boundary, player zero SHALL execute first toward a candidate horizon and stop at any earlier host-originated cable event. It SHALL issue `EXECUTION_GRANT(H)` only after reaching `H`, SHALL keep at most one grant outstanding, and SHALL remain at `H` until player one acknowledges safe progress to `H` or reports an earlier mode intent. Player one SHALL never receive or hold a grant beyond player zero's current cable cycle and SHALL not execute beyond its current grant. Neither peer SHALL apply an authoritative event retroactively.

#### Scenario: Host reaches a candidate horizon
- **WHEN** player zero reaches candidate horizon `H` without an earlier cable event and no grant is outstanding
- **THEN** it may send `EXECUTION_GRANT(H)`
- **AND** it pauses at `H` while the grant is outstanding

#### Scenario: A grant is already outstanding
- **WHEN** player zero is waiting for `GRANT_ACK` or an earlier client intent
- **THEN** it sends no later execution grant
- **AND** it does not advance beyond the outstanding horizon

#### Scenario: Client is granted execution
- **WHEN** player one receives a valid grant for horizon `H`
- **THEN** player zero has already reached `H`
- **AND** player one cannot execute beyond `H`

#### Scenario: Client completes a grant without an intent
- **WHEN** player one safely reaches the granted cable horizon with no pending cable-visible action
- **THEN** it sends the matching `GRANT_ACK`
- **AND** the host may resume first toward the next candidate horizon

#### Scenario: Client records an intent before the horizon
- **WHEN** player one reaches a local cable-visible mode write before its grant horizon
- **THEN** it sends the intent and pauses at that SIO boundary instead of acknowledging safe progress beyond it
- **AND** player zero remains at the already reached current grant boundary

#### Scenario: Host encounters transfer start before the candidate horizon
- **WHEN** player zero writes MULTI start at cycle `T` before reaching candidate horizon `H`
- **THEN** the candidate grant is truncated at `T` and no grant beyond `T` is sent
- **AND** `TRANSFER_START` acts as a client catch-up barrier at `T`

#### Scenario: Client has an earlier mode intent than transfer start
- **WHEN** player one catches up toward transfer cycle `T` but reaches a mode intent at `C < T`
- **THEN** it reports `MODE_INTENT` instead of `TRANSFER_READY`
- **AND** the host resolves that mode generation before continuing or refusing the transfer

#### Scenario: Authoritative event would be in the past
- **WHEN** a peer receives a committed host event for a cable cycle it has already passed
- **THEN** the driver treats the invariant violation as synchronization failure
- **AND** the event is not retroactively applied

#### Scenario: Valid transfer start is delivered
- **WHEN** player one receives the next valid `TRANSFER_START`
- **THEN** its start cycle is not in player one's past by construction
- **AND** packet latency alone cannot create a past-start condition

### Requirement: Barrier-committed MULTI mode readiness
A local mode write SHALL change that GBA's own SIO mode immediately but SHALL change shared cable readiness only through a committed generation. The quiescent attachment snapshots SHALL be submitted as the initial mode intent generation. Outside a transfer, readiness SHALL use blocking `MODE_INTENT`, host-selected `MODE_COMMIT`, and bilateral `MODE_ACK`; player one SHALL remain paused after its acknowledgement until the host releases the next grant. After START emission, a mode write SHALL instead record and transmit a deferred intent, mark the transfer abort-pending, and continue toward the immutable completion cycle without entering the ordinary blocking mode barrier. Deferred generations SHALL commit at completion before the next ordinary execution grant.

#### Scenario: Both peers attach while already in MULTI
- **WHEN** both initial attachment snapshots report MULTI
- **THEN** the initial generation commits joint readiness without waiting for a later local mode write

#### Scenario: Initial snapshots disagree
- **WHEN** only one initial attachment snapshot reports MULTI
- **THEN** the initial generation commits the actual unequal modes
- **AND** the link remains topologically attached but not jointly ready

#### Scenario: Client enters MULTI
- **WHEN** player one locally enters MULTI during an execution grant
- **THEN** player one records and flushes `MODE_INTENT` and pauses at the SIO boundary
- **AND** player zero does not treat player one as MULTI-ready before commit

#### Scenario: Mode commit completes
- **WHEN** both peers acknowledge the same mode generation and commit boundary
- **THEN** shared peer-visible mode state changes atomically at that boundary
- **AND** player one cannot execute an instruction observing it before the host releases the next grant

#### Scenario: A peer leaves MULTI
- **WHEN** either GBA locally selects a different mode outside an active transfer
- **THEN** the same mode-intent barrier commits loss of MULTI readiness
- **AND** no later transfer can begin under the previous mode generation

#### Scenario: Host changes mode during a transfer
- **WHEN** player zero writes a different mode while executing from START cycle `T` toward completion `C`
- **THEN** it records a deferred intent and marks the transfer abort-pending without pausing at the write
- **AND** it remains authorised to reach `C`

#### Scenario: Client changes mode during completion catch-up
- **WHEN** player one writes a different mode while executing under `COMPLETION_CATCHUP` toward `C`
- **THEN** it records and transmits a deferred intent without entering the ordinary mode barrier
- **AND** it reaches `C` and reports the abort in `COMPLETION_READY`

#### Scenario: Client discovers mode change after host reached completion
- **WHEN** player zero is already paused at `C` and player one discovers a mode write while catching up
- **THEN** player one continues to `C`
- **AND** the host selects an erroneous completion before committing the deferred generation

#### Scenario: Mode barrier times out
- **WHEN** the matching mode commit or acknowledgement misses its configured deadline
- **THEN** the session detaches without applying a one-sided peer-ready state

### Requirement: Player-zero common-path transfer initiation
Only player zero SHALL initiate a network MULTI transfer. Its `GBASIODriver.start()` hook SHALL validate committed readiness before allocating a sequence. For a valid start it SHALL capture the outgoing word and SIOCNT settings, calculate the authoritative completion cycle with `GBASIOTransferCycles(GBA_SIO_MULTI, siocnt, 1)`, send a flushed `TRANSFER_START`, and block at the current emulated cycle for readiness or abort. A valid readiness SHALL produce `TRANSFER_COMMIT`; a post-START failure SHALL produce `abort_pending` with the same cycle. The hook SHALL return a common-owned start result carrying effective peer count one after START emission or zero before it, and common mGBA SIO code SHALL schedule accordingly.

#### Scenario: Primary starts a valid transfer
- **WHEN** player zero sets MULTI start while attachment and both modes are committed
- **THEN** the start hook creates one transfer sequence and waits without advancing emulated time
- **AND** successful readiness causes a commit to be sent
- **AND** the hook returns a common-owned start result with effective peer count one
- **AND** common `_startTransfer()` schedules the existing host completion event

#### Scenario: Secondary attempts a local start
- **WHEN** player one writes a MULTI start condition independently
- **THEN** the network driver does not create a transfer sequence
- **AND** player zero remains the sole cable-clock initiator
- **AND** the write retains busy as an existing wait-for-primary condition plus committed secondary line and ID state
- **AND** it creates no network completion event or IRQ until a valid remote primary start arrives

#### Scenario: Primary starts before joint mode readiness
- **WHEN** player zero writes MULTI start while attached but joint MULTI readiness is not committed
- **THEN** no transfer sequence or `TRANSFER_START` is created
- **AND** common SIO follows the characterized ordinary no-driver/no-peer start path

#### Scenario: Ordinary no-peer completion retains baseline behavior
- **WHEN** a pre-START failure releases a primary start to the pinned no-driver/no-peer path
- **THEN** common SIO initializes all receive words to `0xFFFF` and schedules the selected zero-peer duration
- **AND** common completion installs zero receive words, clears busy, leaves error clear, exposes ready one, slave one, ID zero, and RCNT SC high
- **AND** it raises exactly one SIO IRQ when locally enabled

#### Scenario: A prior mode intent exists
- **WHEN** player zero starts a transfer while an earlier mode intent is unresolved
- **THEN** the mode barrier resolves first
- **AND** if the committed modes are not both MULTI, no START is emitted and common SIO follows the ordinary no-peer path

#### Scenario: Peer leaves MULTI after START
- **WHEN** either peer writes a non-MULTI mode after `TRANSFER_START` emission but before completion
- **THEN** the active transfer is marked for erroneous completion without blocking at the mode write
- **AND** both peers remain authorised to reach the announced cycle before the deferred generation commits

#### Scenario: Remote START and local mode write share a cycle
- **WHEN** player one's scheduled remote START event and CPU mode write are due at the same virtual cycle `T`
- **THEN** the already-scheduled START event is processed first
- **AND** the mode write is treated as a post-START deferred intent that makes completion erroneous

#### Scenario: Completion and mode write share a cycle
- **WHEN** the existing SIO completion event and a CPU mode write are due at the same virtual cycle
- **THEN** the already-due common completion runs first according to frozen `mTiming` ordering
- **AND** the subsequent register write creates the next mode intent

#### Scenario: Transfer start path terminates
- **WHEN** any local or remote MULTI start path is processed
- **THEN** it ends in exactly one successful network completion, characterized erroneous completion, ordinary no-driver/no-peer behavior, or reset/unload cancellation
- **AND** no path other than the characterized secondary wait-for-primary condition leaves busy set without a scheduled or cancelled terminal transition

### Requirement: Secondary remote-start scheduling
Upon a valid `TRANSFER_START`, player one SHALL reach the mapped start boundary without passing it, set local MULTI busy, capture `SIOMLT_SEND`, retain the announced completion cycle, schedule the existing `sio->completeEvent` for that cycle, and send exactly one matching `TRANSFER_READY`. If it encounters an earlier unreported mode intent, it SHALL stop there and report the intent instead of accepting START. It SHALL not manually reproduce common completion behavior.

#### Scenario: Secondary handles a transfer start
- **WHEN** player one reaches the start cycle for the next valid transfer sequence
- **THEN** it captures its current outgoing 16-bit word
- **AND** it schedules the existing local SIO completion event for the announced cycle
- **AND** it sends one readiness response

#### Scenario: Secondary encounters earlier mode intent
- **WHEN** player one receives `TRANSFER_START(T)` but its catch-up reaches an uncommitted mode write at `C < T`
- **THEN** it does not set busy or schedule the transfer yet
- **AND** it sends the earlier `MODE_INTENT` for host resolution

#### Scenario: Readiness has the wrong sequence
- **WHEN** player zero receives readiness for a stale, skipped, future, or different transfer sequence
- **THEN** it does not use the supplied word
- **AND** it enters protocol failure handling

### Requirement: Authoritative transfer commit
After valid readiness, player zero SHALL send one reliable flushed commit containing the transfer sequence, authoritative completion cycle, player-zero word, player-one word, and `0xFFFF` for unattached slots two and three. The commit SHALL be a candidate successful result only; it SHALL not authorize player one beyond START cycle `T` and SHALL not be the final outcome commit point.

#### Scenario: Transfer is committed
- **WHEN** player zero receives valid readiness before its operation deadline
- **THEN** it sends the matching commit
- **AND** both peers retain `[player0, player1, 0xFFFF, 0xFFFF]` as the pending successful result

#### Scenario: Client receives transfer commit at START
- **WHEN** player one receives valid `TRANSFER_COMMIT` while paused at `T`
- **THEN** it remains paused at `T`
- **AND** it does not treat commit as an execution grant to `C`

#### Scenario: Exact duplicate commit arrives
- **WHEN** a peer receives an exact duplicate commit for the current transfer
- **THEN** it does not install the words or complete the transfer twice

#### Scenario: Conflicting commit arrives
- **WHEN** a duplicate transfer sequence carries different words or timing
- **THEN** the receiver rejects the session before installing that result

### Requirement: Common-path hardware completion
Completion SHALL be host-led. After host common scheduling, player zero SHALL execute first from START cycle `T` to completion cycle `C`, enter `finishMultiplayer`, send `COMPLETION_CATCHUP` for the transfer/completion sequence and pending outcome, and remain paused at `C`. Only receipt of that message SHALL authorize player one to execute from `T` to `C` while transport is healthy. Player one SHALL enter its completion hook, send `COMPLETION_READY` with local abort/deferred-mode status, and remain paused. Player zero SHALL select and send `COMPLETION_DECISION`; that message SHALL be the authoritative outcome commit point. Player one SHALL validate the decision, send `COMPLETION_DECISION_ACK`, and return its hook. Player zero SHALL remain in its hook until the matching acknowledgement arrives or terminal delivery failure resolves; losing only the final acknowledgement SHALL close the session without rewriting the committed outcome. Each returning hook SHALL let normal mGBA `GBASIOMultiplayerFinishTransfer()` install the decided words, clear busy, restore line and device-ID state, and raise exactly one local serial IRQ when enabled.

#### Scenario: Both peers reach successful completion
- **WHEN** player zero reaches `C`, player one catches up under `COMPLETION_CATCHUP`, player zero receives `COMPLETION_READY`, and player one receives a successful `COMPLETION_DECISION`
- **THEN** each hook returns the committed words to common SIO code
- **AND** both expose identical receive data at the authoritative cycle
- **AND** common code raises one IRQ on each peer whose local IRQ enable is set

#### Scenario: Host reaches completion
- **WHEN** player zero reaches `C` while player one remains paused at `T`
- **THEN** player zero sends one matching `COMPLETION_CATCHUP`
- **AND** player zero remains paused inside its completion hook

#### Scenario: Client catches up to completion
- **WHEN** player one receives valid `COMPLETION_CATCHUP` for `C`
- **THEN** it is authorised to execute only as far as `C`
- **AND** it sends `COMPLETION_READY` and remains paused inside its own completion hook

#### Scenario: Authoritative completion decision arrives
- **WHEN** player one receives a valid `COMPLETION_DECISION` for its current completion sequence
- **THEN** it sends one matching `COMPLETION_DECISION_ACK`
- **AND** it returns the decided success or error result to common SIO exactly once
- **AND** a pending transfer commit alone cannot release the hook

#### Scenario: Host receives the completion decision acknowledgement
- **WHEN** player zero receives the valid acknowledgement for its committed completion sequence
- **THEN** it returns the same decided result to common SIO exactly once
- **AND** the session may return to ready state for the next transfer

#### Scenario: Final acknowledgement is lost
- **WHEN** player one installed the decision and sent its acknowledgement but the host does not receive it
- **THEN** both peers preserve the already committed completion outcome at `C`
- **AND** player zero fails the session at the decision-delivery deadline so no later transfer can start

#### Scenario: Completion peer is late
- **WHEN** player zero sends catch-up but does not receive matching completion readiness while the transport remains healthy
- **THEN** it waits at `C` under the completion-readiness deadline
- **AND** neither hook returns before the authoritative decision

#### Scenario: Healthy decision delivery
- **WHEN** the transport generation remains healthy through delivery of `COMPLETION_DECISION` and its acknowledgement
- **THEN** both hooks return the same authoritative outcome at `C`

#### Scenario: Terminal failure interrupts final decision delivery
- **WHEN** the host's active transport accepts `COMPLETION_DECISION` but the generation terminates before player one receives it
- **THEN** player zero MAY complete with the decided outcome
- **AND** player one error-completes locally at `C` after stop or deadline
- **AND** the resulting asymmetric terminal observation is explicitly permitted

### Requirement: Explicit mid-transfer error completion
Recoverable transport stop, peer detach, protocol failure, mode departure, queue/send failure, or timeout during a MULTI transfer SHALL call the dedicated network abort path and SHALL complete an observable erroneous transfer rather than merely cancelling the completion event. Failure before successful `TRANSFER_START` emission SHALL use the characterized ordinary disconnected/no-peer start path. After START emission, the announced completion cycle SHALL be immutable for success and failure. A reachable peer SHALL send `TRANSFER_ABORT` with transfer sequence, completion cycle, and stable reason; if transport is dead, each side that accepted START SHALL use its retained cycle locally. Reset and unload remain immediate cancellation.

#### Scenario: Recoverable transfer failure completes
- **WHEN** link failure occurs while emulation will continue
- **THEN** `SIOMULTI0` through `SIOMULTI3` become `0xFFFF`
- **AND** SIOCNT busy is cleared and communication error is set
- **AND** SIOCNT ready equals one and slave equals one for disconnected pulled-up lines
- **AND** device ID becomes zero
- **AND** RCNT SC becomes high and idle
- **AND** common completion raises exactly one SIO IRQ if locally enabled

#### Scenario: Failure occurs after commit
- **WHEN** transport fails after a transfer commit but before common completion
- **THEN** the driver preserves the authoritative completion boundary
- **AND** the error result replaces the successful result at that boundary

#### Scenario: Failure occurs before START emission
- **WHEN** readiness validation, mode resolution, queue capacity, or transport state fails before `TRANSFER_START` is successfully emitted
- **THEN** no network transfer exists
- **AND** common SIO uses the characterized disconnected/no-peer start path

#### Scenario: START is received but READY never reaches host
- **WHEN** player one accepts START, schedules completion, and sends READY but host never receives it
- **THEN** both accepting endpoints error-complete at the START-announced cycle
- **AND** the host start result retains effective peer count one so common scheduling selects the two-device duration

#### Scenario: Frontend stops after START before readiness
- **WHEN** a frontend stop occurs after START is accepted and before readiness completes
- **THEN** the accepting endpoint retains the announced cycle locally
- **AND** it error-completes there even though no abort packet can be sent

#### Scenario: Client loses transport before completion catch-up
- **WHEN** player one accepted START but transport terminates while it is still paused at `T`
- **THEN** it locally advances no farther than retained cycle `C`
- **AND** it performs terminal erroneous completion there without a host catch-up message

#### Scenario: Commit cannot be sent
- **WHEN** the host receives valid READY but cannot send `TRANSFER_COMMIT`
- **THEN** the transfer becomes abort-pending at the unchanged announced completion cycle
- **AND** `TRANSFER_ABORT` is sent if the transport remains usable

#### Scenario: Transport stops after commit
- **WHEN** commit has been sent but transport stops before completion
- **THEN** the successful words are replaced by the error result
- **AND** the announced completion cycle is preserved

#### Scenario: Abort notification arrives late
- **WHEN** a peer reaches the announced completion boundary before its matching `TRANSFER_ABORT` arrives
- **THEN** emulated execution pauses at that boundary under the applicable deadline
- **AND** delayed notification does not move the completion to a later cable cycle

#### Scenario: START is emitted but never delivered
- **WHEN** player zero successfully emits START and the remote endpoint dies before accepting it
- **THEN** player zero error-completes at the retained announced cycle
- **AND** player one has no transfer completion to run and performs immediate idle-detach cleanup

#### Scenario: Reset or unload interrupts transfer
- **WHEN** the core resets or unloads while a transfer is pending
- **THEN** the event and network state are cancelled synchronously
- **AND** no completion IRQ is fabricated for state that is being reset or destroyed

#### Scenario: Write follows an error completion
- **WHEN** software performs a subsequent SIO write after error completion
- **THEN** session topology is detached and effective peer count is zero
- **AND** the write follows ordinary no-peer behavior without remaining busy

### Requirement: Deterministic latency-independent verification
Given identical initial emulated states, inputs, save data for each corresponding peer, timing-sensitive profiles, and logical cable events, the driver SHALL commit the same attachment, mode, transfer, completion, and error cycles and produce the same host observations regardless of bounded packet delivery latency or jitter while the transport remains healthy through authoritative decision delivery. Terminal failure during final decision delivery SHALL instead follow the expressly permitted role-specific outcome.

#### Scenario: Mode intent is replayed with varied latency
- **WHEN** a test injects the same logical client mode intent with different delivery delays inside the supported grant/deadline policy
- **THEN** the host does not observe the mode before its committed barrier
- **AND** the committed mode generation and subsequent host control flow are identical

#### Scenario: Transfer trace is replayed with varied latency
- **WHEN** a test replays the same logical transfer with varied bounded packet delivery timing
- **THEN** returned words, device IDs, completion cycles, error state, and IRQ counts are identical

#### Scenario: Host start truncates a candidate horizon
- **WHEN** the same host start cycle occurs under different bounded delivery latency
- **THEN** no client grant exceeds that start cycle
- **AND** START is never a valid event in the client's past

#### Scenario: Post-START failures are replayed
- **WHEN** tests inject failure after START at readiness, commit-send, post-commit, catch-up, and completion-readiness phases before final decision commitment
- **THEN** every endpoint that accepted START uses the same announced completion cycle
- **AND** error registers and IRQ counts are independent of notification latency

#### Scenario: Final decision partition is replayed
- **WHEN** tests terminate transport after the host commits a decision but before client delivery
- **THEN** the trace records the permitted host-decision/client-error terminal outcome
- **AND** neither peer moves its observable completion away from `C`
