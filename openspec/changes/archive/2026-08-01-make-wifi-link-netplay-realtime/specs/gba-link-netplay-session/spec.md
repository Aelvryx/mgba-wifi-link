## ADDED Requirements

### Requirement: Protocol-v2 replicated runtime negotiation
Both peers SHALL advertise the replicated-pair protocol version, capability, and experimental/stable policy before exchanging mutable machine state. The release runtime SHALL require exact matches and SHALL not automatically downgrade to the distributed-SIO protocol. A retained protocol-v1 diagnostic runtime SHALL be explicitly selected on both endpoints and SHALL use a distinct compatibility string.

#### Scenario: Both peers support protocol v2
- **WHEN** both bilateral `HELLO` messages advertise the same supported replicated-pair protocol version and required capabilities
- **THEN** the host may proceed with protocol-v2 replica attachment

#### Scenario: Peer uses protocol v1
- **WHEN** one peer advertises distributed-SIO protocol v1 and the other advertises replicated-pair protocol v2
- **THEN** attachment is rejected before snapshot exchange
- **AND** the user is told that both devices require the same link runtime build and protocol

#### Scenario: Experimental policy differs
- **WHEN** one peer advertises an experimental runtime and the other advertises a stable runtime
- **THEN** attachment is rejected before either endpoint captures or sends a replica bundle

### Requirement: Validated replica bundle exchange
After exact identity and determinism validation, each endpoint SHALL provide one authoritative bundle for its assigned logical player through a manifest followed by bounded reliable chunks. The manifest SHALL version the bundle format and declare uncompressed size, transmitted size, chunk size, save-memory type and size, logical timing metadata, and cryptographic digests. The receiver SHALL allocate only within configured limits and SHALL install nothing until all bytes and digests validate.

#### Scenario: Complete replica bundle arrives
- **WHEN** every non-overlapping chunk declared by a valid manifest arrives and its assembled and uncompressed digests match
- **THEN** the receiver may construct that logical player's replica from the verified bundle
- **AND** it acknowledges the installed logical player and digest

#### Scenario: Replica chunk is invalid
- **WHEN** a chunk is oversized, out of range, overlapping with different bytes, associated with the wrong player or generation, or produces a digest mismatch
- **THEN** the core rejects the attachment without installing partial state
- **AND** all provisional replica buffers are erased during teardown

#### Scenario: Manifest exceeds resource limits
- **WHEN** a manifest declares unsupported state, save, compressed, or uncompressed size
- **THEN** the core rejects it before allocating the declared resource

## MODIFIED Requirements

### Requirement: Atomic session attachment
The core SHALL enter protocol-v2 attachment through bilateral quiescent local-core boundaries and SHALL not expose a replicated cable until exact identity, determinism profile, player assignments, both authoritative replica bundles, input-delay policy, and the attachment generation are acknowledged. The attachment deadline SHALL begin at peer admission. Each peer SHALL pause its existing local core only after any ordinary pending SIO completion ends, capture its assigned-player bundle, and continue no game execution beyond that boundary while attachment is pending. Each endpoint SHALL build a provisional P0/P1 pair from the same verified bundle digests and attach its local coordinator in P0-then-P1 order. Player zero SHALL send `SESSION_READY`; player one SHALL install it and send `SESSION_READY_ACK`; player zero SHALL not execute the pair before that acknowledgement; and player one SHALL remain paused until the first valid input-window release from player zero.

#### Scenario: Replicated attachment completes
- **WHEN** both endpoints acknowledge the same session ID, P0 and P1 bundle digests, assignments, input delay, and attachment generation
- **THEN** both endpoints expose identical P0/P1 local-link topology to game execution
- **AND** each endpoint presents only its assigned player

#### Scenario: Final acknowledgement is in flight
- **WHEN** player one sent `SESSION_READY_ACK` but has not received the first valid input-window release
- **THEN** player one remains paused at the accepted pair boundary
- **AND** its executing games cannot yet observe time after attachment

#### Scenario: Existing completion becomes quiescent in time
- **WHEN** a standalone SIO completion finishes before the admission-started attachment deadline
- **THEN** the session captures the first subsequent quiescent local-core bundle exactly once

#### Scenario: Local SIO never becomes quiescent
- **WHEN** busy state or a scheduled completion persists through the admission-started attachment deadline
- **THEN** no replica bundle is sent after expiry
- **AND** attachment fails with a quiescent-rendezvous diagnostic
- **AND** the original single-player core resumes unchanged and detached

#### Scenario: Snapshot installation fails
- **WHEN** either endpoint cannot construct both provisional cores from the accepted bundles
- **THEN** neither endpoint exposes the replicated cable
- **AND** both discard provisional pairs and retain their original local cores

#### Scenario: Stop interrupts attachment
- **WHEN** transport stops during manifest, chunk, pair construction, or final readiness exchange
- **THEN** callbacks, queues, provisional bundles, and provisional cores for that generation are invalidated
- **AND** the original local core remains the only runnable game

### Requirement: Explicit wire format and sequence domains
All protocol-v2 session-control, replica, input, verification, and detach packets SHALL use reliable ordered Netpacket delivery and explicit fixed-width little-endian encoding. The common header SHALL contain magic, exact protocol version, message type, payload length, session ID, and a per-sender 64-bit packet sequence. Session IDs, replica generations, chunk indexes, input frame numbers, state-check frames, and detach generations SHALL be validated as distinct domains according to each message type. Protocol-v1 grant, mode, transfer, and completion messages SHALL not be valid in a protocol-v2 session.

#### Scenario: Valid protocol-v2 packet is received
- **WHEN** a correctly encoded packet arrives from the expected peer with the expected transport generation, session ID, sender sequence, and message-specific relation
- **THEN** the core applies the packet exactly once according to its protocol-v2 state rules

#### Scenario: Protocol-v1 runtime packet is received
- **WHEN** an execution-grant, mode, transfer, or completion message arrives in a protocol-v2 session
- **THEN** the core rejects the session before applying it to either logical core

#### Scenario: Malformed or spoofed packet is received
- **WHEN** a packet has invalid magic, version, size, sender, transport generation, session ID, field range, sequence relation, role ownership, or state transition
- **THEN** the core does not apply its payload to replica or emulated state
- **AND** the core terminates the affected session with a diagnostic

#### Scenario: A sequence domain reaches its maximum
- **WHEN** any 64-bit session sequence or frame domain would need to wrap
- **THEN** the core cleanly tears down the session
- **AND** a new transport session is required

### Requirement: Operation-specific bounded waits
Handshake, quiescent attachment, replica manifest, replica chunk transfer, pair installation, final readiness, each missing input frame, state verification, and graceful detach SHALL each have an independently configurable wall-clock deadline under the configured safety ceiling. A synchronous runtime rendezvous SHALL flush queued output, poll receive, process the copied inbound queue after every poll, verify transport generation after every frontend callback, yield between empty polls, and never return repeated empty emulation frames while merely waiting.

#### Scenario: Required packet arrives before its operation deadline
- **WHEN** the valid replica, input, check, or control packet for an active wait arrives before its deadline
- **THEN** the core processes it during that wait and resumes the corresponding transition

#### Scenario: An operation times out
- **WHEN** the required packet does not arrive before its configured operation deadline
- **THEN** the core tears down the pending or active replicated session safely
- **AND** neither local pair continues one-sided
- **AND** the diagnostic identifies the operation, logical player where applicable, and frame or chunk relation

#### Scenario: Receive polling is unavailable
- **WHEN** the frontend starts a protocol-v2 session without the receive-poll function required for bounded synchronous rendezvous
- **THEN** the core declines replicated attachment
- **AND** the user receives an unsupported-frontend diagnostic

### Requirement: Session teardown
Stopping transport, unloading content, losing the peer, reset, protocol failure, replica divergence, or operation timeout SHALL transition protocol v2 to a safe disconnected state. Teardown SHALL invalidate transport callbacks and queues before destroying replica state, stop both local logical cores and their coordinator, preserve or restore only the endpoint's assigned local-role state and newest valid owned save generation, and never persist shadow-owned save data. The MVP SHALL not reconnect or migrate the host within the same emulation run.

#### Scenario: Peer disconnects while ready
- **WHEN** the remote peer disconnects between replicated frames
- **THEN** both local logical cores stop before another pair frame executes
- **AND** the endpoint returns to detached single-player state using its local-role machine and save ownership

#### Scenario: Frontend stops re-entrantly
- **WHEN** the frontend invokes the Netpacket stop callback during send or receive polling
- **THEN** the core invalidates the callback pointers and transport generation before further use
- **AND** pair teardown does not invoke stale frontend functions

#### Scenario: Content unloads during a session
- **WHEN** the frontend unloads content while a replicated pair or provisional bundle exists
- **THEN** both cores, the coordinator, bundle buffers, and network session are destroyed before ROM storage

#### Scenario: Reset is requested during a session
- **WHEN** the frontend resets in any non-disconnected state
- **THEN** the session first tears down and retains only local-role ownership
- **AND** normal reset then applies to the restored local core

### Requirement: Time-changing operation safety
The core SHALL reject both user state creation and user state restoration whenever network session state is not `DISCONNECTED`, including transport, handshake, quiescent rendezvous, replica exchange, pair construction, ready, frame rendezvous, running-pair, verification, and failed-before-teardown states. Internal replica bundles SHALL be attachment data only and SHALL not be exposed as user savestates. A reset requested in any non-disconnected state SHALL first complete replicated-pair teardown and then reset the restored local-role core. Serialization and reset behavior while `DISCONNECTED` SHALL remain unchanged.

#### Scenario: State save is requested during replica exchange
- **WHEN** the frontend calls `retro_serialize` during any protocol-v2 attachment or live state
- **THEN** the core returns failure without producing a savestate
- **AND** emulated, replica, and network state remain unchanged

#### Scenario: State load is requested during replicated play
- **WHEN** the frontend calls `retro_unserialize` while a pair exists
- **THEN** the core returns failure without changing either logical core

#### Scenario: Reset is requested during replicated play
- **WHEN** the frontend calls `retro_reset` while a pair exists
- **THEN** the core tears down network and shadow state before resetting the restored local-role core

#### Scenario: State operation occurs while disconnected
- **WHEN** state creation or restoration is requested with no transport or replica session state
- **THEN** existing single-core behavior is preserved
