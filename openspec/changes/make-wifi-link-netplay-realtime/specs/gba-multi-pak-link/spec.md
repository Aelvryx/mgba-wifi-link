## MODIFIED Requirements

### Requirement: GBA MULTI Multi-Pak scope
The protocol-v2 replicated pair SHALL provide two-player Multi-Pak link-cable operation through the existing local GBA SIO lockstep drivers. It SHALL not add network semantics for Single-Pak multiboot, Wireless Adapter/RFU, NORMAL8, NORMAL32, UART, GPIO, or Joy Bus communication in the MVP. Serial-mode handling outside MULTI SHALL remain the behavior of the attached local driver and common SIO code rather than creating protocol-v2 packets.

#### Scenario: Both logical players use MULTI mode
- **WHEN** the replicated P0 and P1 cores select GBA MULTI mode
- **THEN** each endpoint's local lockstep coordinator provides two-player Multi-Pak transfers
- **AND** no serial-mode or transfer message is sent over Netpacket

#### Scenario: A logical player selects another serial mode
- **WHEN** either replicated core selects a serial mode outside GBA MULTI
- **THEN** existing common SIO and local-driver mode gating applies
- **AND** protocol v2 does not fabricate a network transaction for that mode

### Requirement: Hardware-consistent attached player state
Within each replicated pair, logical P0 SHALL be attached as primary/master device ID zero and logical P1 as secondary/slave device ID one. Each endpoint's local lockstep coordinator SHALL be the sole source of connected-device count, MULTI ready/slave/ID line state, transfer duration, receive words, busy clearing, and SIO IRQ timing. Unused player slots two and three SHALL remain unattached. Network-session topology SHALL not override an individual SIO transfer's local coordinator result.

#### Scenario: Ready replicated link is queried
- **WHEN** both logical players are attached and use MULTI mode
- **THEN** P0 reads ID zero and primary status
- **AND** P1 reads ID one and secondary status
- **AND** both observe the line and participant state produced by their local coordinator

#### Scenario: Replicated session detaches
- **WHEN** protocol-v2 teardown removes both drivers from the local coordinator
- **THEN** subsequent local SIO reads expose the characterized disconnected state
- **AND** no stale network topology value selects transfer timing

#### Scenario: Both endpoints execute the same transfer
- **WHEN** identical logical P0/P1 states and inputs start a MULTI transfer on both endpoints
- **THEN** both local coordinators produce identical words, player IDs, completion cycles, busy state, and IRQ counts

## REMOVED Requirements

### Requirement: Separate scheduler, grant, and barrier timing
**Reason**: Protocol v2 does not distribute cable time or issue network execution grants; both cable participants execute under one local coordinator.
**Migration**: Use the replicated frame scheduler and frame-input rendezvous defined by `gba-replicated-link-runtime`. The legacy timing policy remains only in the explicitly selected protocol-v1 diagnostic runtime.

### Requirement: Bidirectionally safe execution grants
**Reason**: Per-endpoint cable horizons are unnecessary once both logical GBAs and all cable events exist locally.
**Migration**: Gate replicated frames on authoritative P0/P1 input availability, then execute both local cores to their common frame boundary.

### Requirement: Barrier-committed MULTI mode readiness
**Reason**: MULTI mode changes no longer cross a transport boundary and are ordered directly by the existing in-process SIO lockstep coordinator.
**Migration**: Attach both local lockstep drivers in deterministic player order and let local SIO events establish readiness.

### Requirement: Player-zero common-path transfer initiation
**Reason**: Protocol v2 does not intercept a primary START to negotiate it with a remote device.
**Migration**: The logical P0 core invokes the existing local lockstep start and common SIO scheduling path on both replicas.

### Requirement: Secondary remote-start scheduling
**Reason**: Logical P1 receives START through its endpoint-local lockstep event queue rather than a network packet.
**Migration**: Use the unchanged local `GBASIOLockstepDriver` remote-start behavior.

### Requirement: Authoritative transfer commit
**Reason**: Transferred words are committed synchronously by each endpoint's deterministic local coordinator and require no wire commit.
**Migration**: Verify endpoint equivalence with periodic pair-state digests instead of committing individual words over Netpacket.

### Requirement: Common-path hardware completion
**Reason**: Network catch-up, readiness, decision, and acknowledgement barriers are the measured source of the commercial-game throughput collapse and are not needed for a local pair.
**Migration**: Retain normal mGBA common completion ownership behind the local lockstep driver; no protocol-v2 message releases an individual completion hook.

### Requirement: Explicit mid-transfer error completion
**Reason**: A transport failure no longer interrupts a distributed SIO transaction because the active transaction is entirely local.
**Migration**: Protocol-v2 failure stops and detaches the complete local pair at a safe scheduler boundary, with save ownership handled by replicated-pair teardown. Protocol-v1 retains its characterized mid-transfer error path while available as a diagnostic fallback.

### Requirement: Deterministic latency-independent verification
**Reason**: The old requirement verifies network-committed cable-event cycles; protocol v2 has no network cable events.
**Migration**: Verify identical replicated P0/P1 state traces from authoritative frame inputs and fail closed on periodic state-digest mismatch as specified by `gba-replicated-link-runtime`.
