## ADDED Requirements

### Requirement: Netpacket link capability registration
For loaded GBA content, the libretro core SHALL register its link-netplay callbacks through the current `RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE` API during game loading. The core SHALL preserve normal single-player and non-networked operation when the frontend does not support the interface or no Netpacket session is active.

#### Scenario: Supported frontend loads GBA content
- **WHEN** the core loads valid GBA content in a frontend that accepts the Netpacket interface
- **THEN** the frontend can start a core-managed link-netplay session
- **AND** the core supplies a link-protocol compatibility version to the frontend

#### Scenario: Netpacket is unavailable
- **WHEN** the core loads GBA content in a frontend that does not support the Netpacket interface
- **THEN** the game continues with existing non-networked SIO behavior
- **AND** the core does not attempt direct network access

### Requirement: Two-player authoritative topology
The session SHALL support exactly two emulated GBAs. The Netpacket host with frontend client ID zero SHALL be GBA player zero and the authoritative session owner, and its single accepted peer SHALL be GBA player one.

#### Scenario: First peer connects
- **WHEN** one peer connects to a hosting core
- **THEN** the host admits the peer to compatibility validation
- **AND** the host proposes itself as GBA player zero and the peer as GBA player one

#### Scenario: Additional peer connects
- **WHEN** another remote peer attempts to join an occupied two-player session
- **THEN** the host rejects that peer without changing the active or pending session

### Requirement: Bilateral pre-session identity exchange
Both peers SHALL send `HELLO` before session acceptance. Each `HELLO` SHALL contain the exact wire-protocol version, required capabilities, effective-ROM SHA-1 and byte length, supported compatibility policies, stable emulation-compatibility version, per-category determinism digests, and the local mode snapshot taken at a quiescent handshake rendezvous. `HELLO`, any pre-acceptance `REJECT`, and `ACCEPT` SHALL use header session ID zero; `ACCEPT` SHALL carry the proposed nonzero session ID in its payload, and every acknowledgement or later packet SHALL use that nonzero ID. Neither peer SHALL expose an attached cable from `HELLO` alone.

#### Scenario: Both HELLO messages arrive
- **WHEN** the host and client receive each other's valid `HELLO`
- **THEN** each peer knows the other's protocol, content identity, supported policy, emulation compatibility, determinism categories, and initial local mode
- **AND** the host may evaluate session acceptance

#### Scenario: Only one HELLO arrives
- **WHEN** either peer has not received the other peer's valid `HELLO`
- **THEN** the session remains pre-acceptance
- **AND** both GBAs observe no attached network cable

#### Scenario: Stop interrupts HELLO exchange
- **WHEN** the frontend stops Netpacket transport before both `HELLO` messages are accepted
- **THEN** all pre-session state and queued packets are discarded
- **AND** no cable attachment occurs

### Requirement: Explicit compatibility policy
The handshake SHALL exchange ROM identity independently from compatibility policy. The host's `ACCEPT` SHALL name the selected policy and compatibility group. The MVP SHALL support only `EXACT_ROM`, requiring identical effective-ROM SHA-1 and byte length; `COMPATIBILITY_GROUP` SHALL remain reserved and SHALL be rejected as unsupported.

#### Scenario: Exact ROM policy succeeds
- **WHEN** both peers support `EXACT_ROM` and report identical effective-ROM SHA-1 and byte length
- **THEN** the host may select `EXACT_ROM` with compatibility group zero

#### Scenario: ROM revisions differ under exact policy
- **WHEN** the peers report different effective-ROM SHA-1 or byte length
- **THEN** the host rejects the MVP handshake
- **AND** both cores expose the cable as disconnected
- **AND** the user receives a ROM-mismatch diagnostic

#### Scenario: Unsupported compatibility group is proposed
- **WHEN** a peer proposes `COMPATIBILITY_GROUP` during the MVP
- **THEN** the host rejects the handshake as an unsupported policy

### Requirement: Exact protocol and determinism validation
Before acceptance, both peers SHALL match the exact wire-protocol version, required capability set, stable `emulation_compatibility_version`, and a versioned set of named per-category determinism digests. The categories SHALL cover BIOS/HLE selection and BIOS identity when applicable, CPU/timing/idle/overclock/speed-hack settings, RTC override mode, and disabled-cheat state. Their canonical encodings SHALL be protocol-defined and SHALL exclude arbitrary serialized configuration, compiler or Android ABI, raw build metadata, save memory, inputs, visual settings, audio filters, and runtime RTC value.

#### Scenario: Compatible profiles match
- **WHEN** protocol, required capabilities, timing-sensitive settings, and disabled-cheat state match
- **THEN** compatibility validation succeeds without requiring equal save data or inputs

#### Scenario: Protocol versions differ
- **WHEN** the peers report different exact wire-protocol versions or required capabilities
- **THEN** the host rejects the handshake
- **AND** the user receives a protocol-incompatibility diagnostic

#### Scenario: Determinism categories differ
- **WHEN** the peers report a different digest for one or more named determinism categories
- **THEN** the host rejects the handshake
- **AND** each local log identifies the mismatching category without sending configuration contents

#### Scenario: Harmless build environments differ
- **WHEN** peers have different compiler, Android ABI, or non-semantic build metadata but the stable emulation-compatibility version and all determinism categories match
- **THEN** compatibility validation does not reject them for those harmless build differences

#### Scenario: Cheats are enabled
- **WHEN** either peer reports active cheats
- **THEN** the MVP handshake is rejected before cable attachment

### Requirement: Atomic session attachment
The core SHALL attach the network SIO driver only through an acknowledged common attach barrier entered from quiescent local SIO boundaries. Initial peer admission SHALL create and start the transport-neutral session and its attachment deadline before a quiescent snapshot is available. The session SHALL repeatedly request that snapshot while allowing a pre-existing ordinary transfer to finish. Before sending `HELLO`, each core SHALL pause precisely at a boundary with no active or scheduled SIO completion, MULTI busy clear, and no reset or unload transition; it SHALL snapshot the current local SIO mode there. The adapter SHALL not impose a separate unbounded quiescence preflight. A pending ordinary no-driver/no-peer transfer SHALL finish before attachment or the session-owned attachment deadline SHALL fail. After bilateral `HELLO`, the host SHALL send `ACCEPT` with a nonzero session ID, assignments, selected policy, and attach cycle; the client SHALL install it and send `ACCEPT_ACK`; the host SHALL send `SESSION_READY` with the initial mode generation; and the client SHALL send `SESSION_READY_ACK`. Player zero SHALL not pass the attach cycle before receiving the final acknowledgement. After sending the acknowledgement, player one SHALL remain paused and SHALL not expose attachment to executing GBA software until the first valid post-attachment initial-mode commit or execution grant arrives.

#### Scenario: Atomic attachment completes
- **WHEN** both peers complete `ACCEPT`, `ACCEPT_ACK`, `SESSION_READY`, and `SESSION_READY_ACK` for the same nonzero session ID and attach cycle and player one receives the first valid post-attachment host release
- **THEN** either peer MAY already have installed pending internal driver state
- **AND** both expose the committed attached topology to executing GBA software only from the same acknowledged attachment generation

#### Scenario: Final acknowledgement is in flight
- **WHEN** player one has sent `SESSION_READY_ACK` but has not received a valid post-attachment host event
- **THEN** player one remains paused at the attachment cycle
- **AND** executing GBA software cannot yet observe the attached state

#### Scenario: Existing no-peer completion is pending
- **WHEN** attachment is requested while a local SIO completion event is scheduled or MULTI busy is set
- **THEN** that core does not install the network driver over the pending transfer
- **AND** it waits for the existing completion only until the attachment deadline
- **AND** it sends `HELLO` only if a quiescent snapshot succeeds before that deadline

#### Scenario: Pending completion becomes quiescent in time
- **WHEN** a scheduled standalone SIO completion finishes before the admission-started attachment deadline
- **THEN** the session accepts the first subsequent quiescent snapshot
- **AND** the core pauses at that snapshot boundary and sends exactly one `HELLO`

#### Scenario: SIO never becomes quiescent
- **WHEN** MULTI remains busy or a completion remains pending through the admission-started attachment deadline
- **THEN** attachment fails with a quiescent-rendezvous timeout diagnostic
- **AND** no `HELLO` is sent after expiry
- **AND** current-generation callbacks, transport queues, and provisional pre-admission packets are invalidated
- **AND** emulation resumes with the cable detached

#### Scenario: Both games already use MULTI
- **WHEN** both quiescent rendezvous snapshots report MULTI before driver installation
- **THEN** those snapshots are submitted as the initial mode generation
- **AND** joint readiness does not depend on a later `setMode` callback

#### Scenario: Initial modes differ
- **WHEN** one quiescent rendezvous snapshot reports MULTI and the other reports a different mode
- **THEN** the initial mode generation commits the unequal modes
- **AND** the attached topology does not expose joint MULTI readiness

#### Scenario: Mode write approaches attachment
- **WHEN** a game writes SIO mode while entering the attachment rendezvous
- **THEN** existing mGBA event ordering determines whether the write precedes the quiescent snapshot
- **AND** emulation remains paused after that snapshot so no later mode write can race the barrier

#### Scenario: ACCEPT is not acknowledged
- **WHEN** the host sends `ACCEPT` but does not receive the matching `ACCEPT_ACK`
- **THEN** the host does not expose a connected cable
- **AND** it remains blocked only until the attachment deadline

#### Scenario: SESSION_READY is not acknowledged
- **WHEN** the host sends `SESSION_READY` but does not receive the matching final acknowledgement
- **THEN** neither side may progress past the proposed attach barrier as an attached session
- **AND** expiry aborts attachment

#### Scenario: Stop interrupts attachment
- **WHEN** transport stops between `ACCEPT` and `SESSION_READY_ACK`
- **THEN** both peers discard the pending session generation
- **AND** both GBAs remain detached

#### Scenario: Handshake packets have different latency
- **WHEN** packet delivery varies after both peers enter their quiescent rendezvous
- **THEN** neither GBA advances beyond its paused rendezvous because of that latency
- **AND** latency-independent attachment guarantees apply from those rendezvous boundaries onward

### Requirement: Explicit wire format and sequence domains
All session-control, grant, barrier, and transfer packets SHALL use reliable ordered Netpacket delivery and explicit fixed-width little-endian encoding. The common header SHALL contain magic, exact protocol version, message type, payload length, session ID, and a per-sender 64-bit packet sequence. Session IDs, grant sequences, mode generations, transfer sequences, completion-barrier sequences, and health-barrier sequences SHALL be distinct 64-bit domains.

#### Scenario: Valid packet is received
- **WHEN** a correctly encoded packet arrives from the expected peer with the expected transport generation, session ID, packet sequence, and message-specific sequence
- **THEN** the core applies the packet exactly once according to that message type's state rules

#### Scenario: Malformed or spoofed packet is received
- **WHEN** a packet has invalid magic, version, size, sender, transport generation, session ID, field range, sequence relation, or state transition
- **THEN** the core does not apply its payload to emulated SIO state
- **AND** the core rejects or terminates the affected session with a diagnostic

#### Scenario: A sequence domain reaches its maximum
- **WHEN** any 64-bit session sequence would need to wrap
- **THEN** the core cleanly detaches the session
- **AND** a new transport session is required before further network link play

### Requirement: Defined duplicate handling
Exact duplicates SHALL be handled according to message type without advancing state twice. Pre-session `HELLO`, attachment acknowledgements, and the most recent idempotent response MAY cause the corresponding response to be resent. Conflicting duplicates SHALL fail the session.

#### Scenario: Exact pre-session duplicate arrives
- **WHEN** an exact duplicate `HELLO` or attachment message arrives with the same sender sequence and payload
- **THEN** the receiver does not install state twice
- **AND** it may resend the already determined response

#### Scenario: Conflicting duplicate arrives
- **WHEN** a packet reuses an already observed sequence with different content
- **THEN** the receiver rejects the protocol state
- **AND** no emulated cable state is changed from that payload

### Requirement: Transport-generation safety
Every Netpacket start SHALL create a new local transport generation. Stop SHALL invalidate that generation, clear its queues, and invalidate all stored frontend function pointers. After each receive-poll call, the core SHALL process copied input and recheck transport generation and stop state before using any saved callback.

#### Scenario: Receive poll delivers packets
- **WHEN** `poll_receive_fn` synchronously invokes one or more receive callbacks
- **THEN** the core copies and processes those packets before polling again
- **AND** it then verifies that the transport generation is still active

#### Scenario: Receive poll synchronously stops transport
- **WHEN** `poll_receive_fn` causes the frontend to invoke the stop callback
- **THEN** the core exits the wait without calling the invalidated send or poll pointer again
- **AND** pending work from that generation is discarded

#### Scenario: Packet from an old generation remains queued
- **WHEN** a queued packet belongs to a transport generation that has stopped
- **THEN** the core discards it without parsing it into the new session

### Requirement: Fail-closed transport buffering
Inbound and outbound copied queues SHALL be bounded. Inbound queue exhaustion, outbound queue exhaustion or send failure, an oversized copied packet, or impossible callback ordering SHALL immediately invalidate the current transport generation. The core SHALL never silently drop a packet assumed to be reliably ordered and SHALL enter the failure path appropriate to the current session and transfer phase.

#### Scenario: Inbound queue is full
- **WHEN** a receive callback cannot copy a valid-sized packet because the bounded inbound queue is exhausted
- **THEN** the core invalidates the current transport generation immediately
- **AND** it does not continue until a later synchronization timeout

#### Scenario: Outbound send fails
- **WHEN** an outbound packet cannot be queued or accepted by the active transport
- **THEN** the core invalidates the current transport generation
- **AND** it selects pending-session, idle-detach, pre-START, or post-START failure semantics from the current state

#### Scenario: Copied packet is oversized
- **WHEN** a callback supplies a packet larger than the declared protocol or copied-queue limit
- **THEN** the core rejects it without truncation
- **AND** the current transport generation fails closed

#### Scenario: Callback ordering is impossible
- **WHEN** a callback arrives for a stopped generation or violates the registered lifecycle ordering
- **THEN** the core does not mutate session or SIO state from that callback
- **AND** any still-current inconsistent generation is invalidated

### Requirement: Operation-specific bounded waits
Handshake, attachment, mode barrier, transfer readiness, transfer commit, completion catch-up/readiness, completion-decision delivery, and graceful detach SHALL each have an independently configurable wall-clock deadline under a three-second safety ceiling. The attachment deadline SHALL begin when the peer is admitted and SHALL include the pre-HELLO wait for a quiescent SIO snapshot. A wait SHALL flush queued output, poll receive, process the queue after every poll, yield between empty polls, and never continue indefinitely.

#### Scenario: Required packet arrives before its operation deadline
- **WHEN** the valid packet for the active wait arrives before that operation's deadline
- **THEN** the core resumes the corresponding protocol or cable transition

#### Scenario: An operation times out
- **WHEN** the required packet does not arrive before its configured operation deadline
- **THEN** the core aborts the pending or active session safely
- **AND** emulation is released according to whether a transfer is active
- **AND** the user receives a link-timeout message
- **AND** the log identifies the timed-out operation

#### Scenario: Receive polling is unavailable
- **WHEN** the frontend starts a session without the receive-poll function required for hard synchronization
- **THEN** the core declines to attach the network SIO driver
- **AND** the user receives an unsupported-frontend diagnostic

### Requirement: Session teardown
Stopping transport, unloading content, losing the peer, reset, or protocol failure SHALL transition the session to a safe disconnected state. Outside a transfer, detach SHALL immediately install the hardware-characterized disconnected SIOCNT ID/ready/slave and RCNT SC line state while preserving the communication-error flag and receive words and raising no transfer IRQ. The MVP SHALL not reconnect or migrate the host within the same emulation run.

#### Scenario: Peer disconnects while idle
- **WHEN** the remote peer disconnects outside a transfer
- **THEN** the remaining core detaches the emulated cable
- **AND** an immediate register read observes characterized disconnected line state without a preceding SIOCNT write
- **AND** no SIO completion IRQ is raised

#### Scenario: Frontend stops the session
- **WHEN** the frontend invokes the Netpacket stop callback
- **THEN** the core ceases all packet sends and receive polling for that generation
- **AND** session queues, mappings, and pending non-transfer barriers are cleared

#### Scenario: Content unloads during a session
- **WHEN** the frontend unloads the game while a session exists
- **THEN** the network SIO driver is torn down before the GBA core and ROM storage are destroyed

### Requirement: Actionable session diagnostics
The core SHALL log and display concise diagnostics for lifecycle readiness, player assignment, ROM mismatch, policy mismatch, named determinism-category mismatch, protocol mismatch, frozen configuration changes, unsupported frontend behavior, malformed packets, queue/send failure, disconnects, and operation-specific timeouts without exposing ROM contents, save data, or private configuration contents.

#### Scenario: Session failure is reported
- **WHEN** the core rejects or aborts a session
- **THEN** the frontend log and user-facing message identify the failure category
- **AND** diagnostics contain no ROM payload, save-memory payload, or configuration value payload

### Requirement: Timing-sensitive configuration remains frozen
From `TRANSPORT_STARTED` through every handshake, attachment, ready, transferring, and failed-but-not-yet-torn-down state, timing-sensitive core-variable changes and cheat API requests SHALL not take effect. The core SHALL reject or ignore the request with a diagnostic instructing the user to disconnect first. Existing behavior for settings excluded from determinism compatibility SHALL remain unchanged.

#### Scenario: Timing option changes during HELLO exchange
- **WHEN** the frontend reports a CPU, timing, idle, overclock, speed-hack, BIOS/HLE-sensitive, or RTC-override core-variable change while the session is not `DISCONNECTED`
- **THEN** the core leaves the effective setting and accepted profile unchanged
- **AND** it reports that link netplay must be disconnected before applying the change

#### Scenario: Cheat API is invoked during a live session
- **WHEN** the frontend invokes a cheat API while the session is not `DISCONNECTED`
- **THEN** the requested cheat change does not take effect
- **AND** the session cannot continue with a silently changed determinism profile

#### Scenario: Visual option changes during a live session
- **WHEN** the frontend changes an option explicitly excluded from determinism compatibility
- **THEN** existing option-update behavior is preserved

### Requirement: Time-changing operation safety
The core SHALL reject both state creation and state restoration whenever network session state is not `DISCONNECTED`, including `TRANSPORT_STARTED`, `HELLO_EXCHANGED`, `ACCEPTED`, `ATTACH_BARRIER`, `READY`, `TRANSFERRING`, and `FAILED` before teardown. Network transport, queues, sequence counters, and peer context SHALL remain excluded from savestates, so no state containing attached SIO or pending-transfer state may be created without its required network context. A core reset requested in any such state SHALL first tear down transport/session state and then reset. Serialization and reset behavior while `DISCONNECTED` SHALL remain unchanged.

#### Scenario: State save is requested during link netplay
- **WHEN** the frontend calls `retro_serialize` in any non-disconnected network session state
- **THEN** the core returns failure without producing a savestate
- **AND** emulated and network state remain unchanged

#### Scenario: State load is requested during link netplay
- **WHEN** the frontend calls `retro_unserialize` in any non-disconnected network session state
- **THEN** the core returns failure without changing emulated or network state

#### Scenario: Reset is requested during link netplay
- **WHEN** the frontend calls `retro_reset` in any non-disconnected network session state
- **THEN** the core tears down the network session before resetting the GBA

#### Scenario: State load is requested outside link netplay
- **WHEN** the frontend calls `retro_unserialize` with no attached network session
- **THEN** existing state-load behavior is preserved

#### Scenario: State save is requested outside link netplay
- **WHEN** the frontend calls `retro_serialize` while session state is `DISCONNECTED`
- **THEN** existing state-save behavior is preserved
