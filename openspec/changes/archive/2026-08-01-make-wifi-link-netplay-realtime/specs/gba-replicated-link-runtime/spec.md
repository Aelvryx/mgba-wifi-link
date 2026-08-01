## ADDED Requirements

### Requirement: Identical two-player replica topology
During a protocol-v2 link session, each endpoint SHALL run exactly two logical GBA cores, P0 and P1, attached in that order to an endpoint-local `GBASIOLockstepCoordinator`. Both endpoints SHALL construct each logical player from the same verified authoritative replica bundle and SHALL not expose the pair to executing game software until both logical-player bundle digests and the attachment generation agree.

#### Scenario: Replicated pair attaches
- **WHEN** both endpoints have verified and installed the authoritative P0 and P1 bundles
- **THEN** each endpoint constructs the pair in P0-then-P1 order
- **AND** the resulting logical player IDs, snapshots, save bytes, and coordinator starting state match across endpoints
- **AND** guest-visible SIOCNT and RCNT lines expose attached P0/P1 roles before either logical CPU executes

#### Scenario: One logical bundle differs
- **WHEN** an endpoint's installed bundle digest for P0 or P1 differs from its peer's accepted digest
- **THEN** neither endpoint exposes an attached replicated cable
- **AND** the attachment fails with the logical player and digest mismatch identified

### Requirement: Local lockstep owns all cable traffic
Protocol v2 SHALL execute GBA MULTI mode changes, transfer starts, serial words, completion timing, receive registers, device IDs, and SIO IRQs entirely through the existing in-process GBA SIO lockstep coordinator on each endpoint. Ordinary protocol-v2 runtime traffic SHALL contain no execution-grant, mode, transfer, or completion packets and SHALL not wait on network delivery for an individual SIO word.

#### Scenario: Commercial game streams serial words
- **WHEN** either logical P0 initiates one or thousands of dependent MULTI transfers during a frame
- **THEN** both local pairs resolve those transfers through their local coordinators
- **AND** the number of network messages is independent of the number of serial words

#### Scenario: Unsupported serial mode is used
- **WHEN** either logical game uses a serial mode outside the supported Multi-Pak scope
- **THEN** existing local lockstep and common SIO mode handling applies
- **AND** protocol v2 does not create a network cable transaction for that access

### Requirement: Fixed-delay authoritative frame inputs
The accepted session SHALL assign physical host input to logical P0 and physical client input to logical P1 and SHALL select one immutable input delay `D` in whole frames. At replicated frame `F`, each endpoint SHALL author its owned input for frame `F + D`; frame `F` SHALL execute only after authoritative P0 and P1 inputs for `F` are installed. The MVP SHALL not predict a missing input.

#### Scenario: Both frame inputs are ready
- **WHEN** authoritative P0 and P1 inputs for frame `F` are installed
- **THEN** both endpoints apply the same inputs to the same logical cores
- **AND** each endpoint advances its pair exactly once to frame `F + 1`

#### Scenario: Remote frame input is late
- **WHEN** local input for frame `F` exists but the remote-owned input does not
- **THEN** neither logical core advances frame `F`
- **AND** the adapter waits only until the input-frame deadline without guessing or repeating the remote input

#### Scenario: Duplicate frame input conflicts
- **WHEN** a sender supplies a different input value for a frame already authored by that sender
- **THEN** the receiver fails the session before executing that frame
- **AND** the diagnostic identifies the sender and frame

#### Scenario: Sender authors the other player
- **WHEN** a host packet attempts to author P1 or a client packet attempts to author P0
- **THEN** the receiver rejects the packet and fails closed

### Requirement: Deterministic pair-frame scheduler
A transport-independent replicated-pair scheduler SHALL set both logical inputs and advance P0 and P1 to persistent per-core next video-frame targets while servicing all local lockstep sleep and wake transitions. The targets SHALL be initialized from each restored core's own frame counter. Repeated execution from identical P0/P1 bundles and frame inputs SHALL produce identical logical-player state traces regardless of endpoint role.

#### Scenario: Pair executes a transfer-heavy frame
- **WHEN** both inputs are available and the games perform multiple SIO transfers before the next frame boundary
- **THEN** the scheduler runs whichever logical core is runnable until both reach their targets
- **AND** it neither deadlocks nor lets one core bypass a required local lockstep wait

#### Scenario: Cable servicing crosses one additional boundary
- **WHEN** a logical core has reached its target but must continue to satisfy a local cable dependency before its peer reaches its target
- **THEN** the scheduler accepts at most one additional video boundary after both targets have been reached
- **AND** it records the recovered lead, rebases that core's next target from its observed counter, and advances the replicated pair frame exactly once

#### Scenario: A logical core advances too far
- **WHEN** either logical core is observed more than one video boundary beyond its current target
- **THEN** the scheduler fails closed with its starting, target, and observed frame counters
- **AND** neither endpoint continues a replica independently

#### Scenario: Endpoint roles are reversed
- **WHEN** one test pair presents P0 and another presents P1 from identical bundles and inputs
- **THEN** their complete P0 state traces match one another
- **AND** their complete P1 state traces match one another

### Requirement: Bounded in-call frontend rendezvous
While a protocol-v2 session is ready, `retro_run()` SHALL process generation-safe receive polling until the current replicated frame becomes runnable, an operation deadline expires, or synchronous transport stop invalidates the generation. A returned successful runtime call SHALL have advanced exactly one replicated pair frame and SHALL provide the local role's newly produced output, including any bounded video-boundary lead required by local cable servicing. It SHALL not return repeated cached video with empty audio merely because a peer input is pending.

#### Scenario: Input arrives during receive polling
- **WHEN** the missing current-frame input arrives before its deadline during `poll_receive_fn`
- **THEN** the copied inbound queue is processed in that same `retro_run()` call
- **AND** one emulated frame runs and supplies its normal audio/video output before the call returns

#### Scenario: Stop re-enters receive polling
- **WHEN** the frontend synchronously invokes stop from inside receive polling
- **THEN** the adapter invalidates callbacks and transport generation before further use
- **AND** the waiting call exits through session teardown without invoking a stale callback

#### Scenario: Input deadline expires
- **WHEN** the remote input remains unavailable through the configured deadline
- **THEN** the session fails with the missing logical player and frame number
- **AND** neither replica continues independently

### Requirement: Role-specific input and presentation
The host endpoint SHALL present P0 and the client endpoint SHALL present P1. Only the presented logical core SHALL receive physical input, supply video and audio, and expose frontend-facing sensor, rumble, camera, and solar behavior. Shadow output SHALL be drained or discarded without invoking duplicate frontend effects.

#### Scenario: Client renders a frame
- **WHEN** the client finishes replicated frame `F`
- **THEN** RetroArch receives P1 video and audio for that frame
- **AND** P0 shadow output does not reach frontend callbacks

#### Scenario: Local controller is sampled
- **WHEN** RetroArch polls the host or client controller
- **THEN** the value authors only that endpoint's assigned logical player
- **AND** the same physical sample is not read again by the shadow core

### Requirement: Local save ownership
P0 save persistence SHALL belong exclusively to the host device and P1 save persistence SHALL belong exclusively to the client device. Shadow save memory SHALL be volatile, memory-backed, and unable to open or overwrite the endpoint's normal save path. The attachment checkpoint and every jointly accepted periodic checkpoint SHALL atomically contain local-role machine state, save-controller state, owned save bytes, RTC metadata, frame, and save generation. On uncertain teardown, the endpoint SHALL restore all fields from its newest complete valid checkpoint or retain the prior complete checkpoint; a generation number alone SHALL NOT make newer save bytes eligible for persistence.

#### Scenario: Both players change save data
- **WHEN** P0 and P1 each modify cartridge save memory during a session
- **THEN** both endpoint replicas observe identical logical save data while connected
- **AND** the host persists only P0 while the client persists only P1

#### Scenario: Session fails after divergence
- **WHEN** replica verification fails before a local save generation is established as safe
- **THEN** the endpoint preserves its last valid local-owned save
- **AND** it does not replace that save with shadow-owned or uncertain data

#### Scenario: Checkpoint replacement cannot allocate
- **WHEN** a periodic verified checkpoint cannot allocate or copy every required state and save region
- **THEN** the prior complete checkpoint remains valid and unchanged
- **AND** teardown cannot combine the failed replacement's save bytes with the prior machine state

#### Scenario: Shadow core is destroyed
- **WHEN** session teardown destroys the non-presented logical core
- **THEN** no shadow save path is flushed to frontend storage

### Requirement: Periodic replica verification
At an agreed frame interval, each endpoint SHALL compute and exchange versioned canonical state digests for logical P0 and P1. The digest input SHALL include emulated state that can affect future execution and SHALL exclude presentation buffers, host pointers, caches, wall clock, transport state, and file-path metadata. A mismatch at the same frame SHALL fail closed before a later input window is released.

#### Scenario: State checks match
- **WHEN** both endpoints exchange identical P0 and P1 digests for verification frame `F`
- **THEN** the session records the successful check and continues

#### Scenario: One player state diverges
- **WHEN** the P1 digest differs at the same verification frame
- **THEN** both peers stop replicated execution before releasing later frames
- **AND** diagnostics contain frame `F`, logical P1, both digests, and the recent input window

### Requirement: Safe replicated-pair teardown
Peer loss, transport stop, protocol failure, timeout, reset, or content unload SHALL stop both logical cores and destroy their local coordinator without leaving a runnable one-sided pair. Reset SHALL first complete session teardown and then reset the restored single local-role core. Content unload SHALL destroy both cores and all replica buffers before ROM storage is released.

#### Scenario: Peer disconnects between frames
- **WHEN** transport ends while neither pair is executing a frame
- **THEN** both local logical cores stop before another frame runs
- **AND** the endpoint restores disconnected single-player operation using its local-role state and save ownership

#### Scenario: Peer disconnects during local SIO traffic
- **WHEN** transport ends while the two local cores are resolving cable transfers inside a frame
- **THEN** the scheduler brings its local pair to a safe stop without waiting for a network SIO decision
- **AND** it does not allow either core to continue alone as an attached cable

### Requirement: Real-time release qualification
The replicated runtime SHALL not become the release default until the exact release core passes automated determinism tests and a two-device Android qualification. On the supported LAN/device class it SHALL sustain at least 59 emulated displayed frames per second during idle connection and continuous MULTI traffic, avoid recurring empty-audio frontend returns, keep serial throughput within five percent of the same build's local two-core baseline, and emit ordinary network traffic proportional to video frames rather than serial words.

#### Scenario: Continuous transfer test ROM runs
- **WHEN** the test ROM streams MULTI words for 30 minutes on the exact two-device release build
- **THEN** both endpoints sustain the frame-rate and serial-throughput gates
- **AND** no periodic state check differs
- **AND** no protocol-v1 transfer packet is emitted

#### Scenario: Commercial qualification succeeds
- **WHEN** a fresh session enters and plays multiplayer in at least one selected commercial Multi-Pak title
- **THEN** both devices retain usable audio, controls, and real-time gameplay
- **AND** the evidence records input delay, p95 rendezvous, packet rate, CPU, peak memory, temperature, and thermal-throttling state

#### Scenario: An attempted title does not link
- **WHEN** an attempted commercial qualification title does not enter multiplayer
- **THEN** the experimental alpha documents the title as a known compatibility failure
- **AND** real-time discovery dwell alone is not reported as a successful game link or evidence of broad compatibility
