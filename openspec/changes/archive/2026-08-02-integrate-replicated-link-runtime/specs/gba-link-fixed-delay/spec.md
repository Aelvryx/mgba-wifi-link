## MODIFIED Requirements

### Requirement: Latency calibration excludes replica work
After compatible bilateral `HELLO` and before `ACCEPT` or replica capture, the GBA Wi-Fi Link session SHALL perform a dedicated bounded latency-calibration phase. For every probe, `T0` SHALL be sampled after complete encoding and immediately before invoking the reliable flushed send callback. `T1` SHALL be sampled after the matching ACK has been received, copied, popped, decoded, and validated but before logging, formatting, the next probe, or optional work. The receiver SHALL copy, pop, decode, and validate identity/ordinal, then encode and send the ACK before diagnostics or optional work. Replica capture, serialization, compression, manifest exchange, installation, and optional logging SHALL occur outside the measured interval.

#### Scenario: Snapshot work cannot inflate RTT
- **WHEN** replica capture is artificially delayed while transport timing remains unchanged
- **THEN** the calibration samples and selected fixed input delay remain unchanged

#### Scenario: Calibration occurs before mutable replica exchange
- **WHEN** both profiles are compatible
- **THEN** the peers complete latency calibration before either authoritative replica is captured or transmitted

#### Scenario: Guest remains paused during calibration
- **WHEN** calibration spans multiple frontend callbacks
- **THEN** neither guest executes past the accepted quiescent boundary

#### Scenario: Optional receiver work follows ACK
- **WHEN** a valid probe reaches the receiver
- **THEN** its matching ACK is sent before diagnostics, formatting, capture, compression, installation, or another optional operation begins

### Requirement: Monotonic timestamp acquisition is fallible and portable
Calibration SHALL obtain timestamps through `bool monotonicTimeUs(void* context, uint64_t* timestamp)` or an equivalent interface with separate success and value. The initiator SHALL fully encode the probe, commit expected ordinal and outstanding-probe state, successfully read `T0`, and only then invoke the send callback. It SHALL successfully read `T1` after ACK validation. A failed read SHALL produce `CALIBRATION_CLOCK_FAILURE`; two successful equal readings SHALL produce a valid zero-duration sample; `T1 < T0`, subtraction overflow, or elapsed time above 1,000,000 microseconds SHALL fail calibration. GBA Wi-Fi Link builds SHALL provide conforming POSIX/Android and Windows implementations or decline to register the runtime on the unsupported platform.

#### Scenario: Clock failure differs from zero duration
- **WHEN** either timestamp acquisition returns failure
- **THEN** calibration fails as `CALIBRATION_CLOCK_FAILURE` and does not insert a zero sample

#### Scenario: Equal successful readings are valid
- **WHEN** both timestamp acquisitions succeed and `T1 == T0`
- **THEN** calibration inserts a valid zero-microsecond sample

#### Scenario: Clock moves backward
- **WHEN** both reads succeed but `T1 < T0`
- **THEN** calibration fails as a monotonic-clock failure without unsigned subtraction

#### Scenario: Send re-entry observes committed probe state
- **WHEN** the reliable send callback synchronously stops or invalidates the transport
- **THEN** the expected ordinal, outstanding-probe state, and successful `T0` already describe the attempted wire probe before failure handling runs

#### Scenario: Supported platform lacks a conforming clock
- **WHEN** GBA Wi-Fi Link is built on POSIX/Android, Windows, or another target without a fallible monotonic microsecond implementation
- **THEN** the runtime does not advertise or register on that target
