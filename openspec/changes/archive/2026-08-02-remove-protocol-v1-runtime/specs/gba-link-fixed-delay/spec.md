## MODIFIED Requirements

### Requirement: Latency policy is protocol-versioned
Calibration message formats, selection policy, production-floor identity, and committed input delay SHALL be covered by the experimental protocol-v2 runtime compatibility version. Peers with an older or incompatible protocol-v2 policy SHALL fail during `HELLO`; the shipped core SHALL provide no alternate GBA link runtime and SHALL NOT automatically downgrade or attach with a mixed delay policy.

#### Scenario: Mixed latency policies reject
- **WHEN** peers advertise different required calibration or selection-policy versions
- **THEN** attachment fails before calibration or replica capture

#### Scenario: Retired protocol-v1 peer cannot attach
- **WHEN** a peer advertises or sends the retired distributed-SIO protocol v1 during attachment to a current core
- **THEN** protocol-v2 validation rejects the attachment before mutable state exchange and no fallback runtime is selected
