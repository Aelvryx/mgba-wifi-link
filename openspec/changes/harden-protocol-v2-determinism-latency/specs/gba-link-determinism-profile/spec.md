## ADDED Requirements

### Requirement: Canonical deterministic configuration profile
Before replica capture, each endpoint SHALL construct a versioned peer-equal deterministic configuration profile from explicitly enumerated, fixed-width fields covering BIOS/HLE identity, emulation and CPU timing policy, overclock and speed-hack policy, idle-loop optimization, input-direction filtering, RTC-normalization/fake-epoch/semantics-model policy versions, enabled cheats, authoritative-input format version, and cartridge-required external inputs. Local RTC-source, time-semantics, and synchronized-input support SHALL be negotiated as raw `HELLO` capabilities rather than included in exact profile equality. The profile SHALL exclude presentation settings, controller mappings, save contents, paths, compiler or ABI identity, harmless build metadata, and unused local capability supersets.

Profile schema version 1 SHALL begin with little-endian `uint16 schema_version = 1` and little-endian `uint16 record_count`, followed by no more than sixteen records in strictly ascending category-ID order. Every 36-byte record SHALL be little-endian `uint16 category`, little-endian `uint16 flags`, and a 32-byte SHA-256 digest. Flag bit zero SHALL mean `REQUIRED`; all other flag bits SHALL be zero. Zero records, more than sixteen records, duplicate/out-of-order records, unknown enum values or bits, noncanonical Booleans, nonzero reserved bytes, and unknown required categories SHALL reject; unknown optional categories SHALL be ignored for equality. Required IDs 1–7 SHALL be BIOS/HLE, CPU/timing, idle optimization, input policy, RTC normalization policy, cheats, and external authoritative-input policy. Their SHA-256 domain suffixes SHALL respectively be `bios-v2`, `cpu-timing-v2`, `idle-optimization-v2`, `input-policy-v2`, `rtc-normalization-v2`, `cheats-v2`, and `external-input-v2`.

Every category digest SHALL be SHA-256 over `mgba-gba-link-replicated-v2`, zero, `determinism-profile-v1`, zero, its suffix, zero, and exactly this schema-v1 payload:

| ID | Canonical payload |
| ---: | --- |
| 1 | `uint8 bios_mode` (`0 = HLE`, `1 = external BIOS`), `uint8 reserved[3]`, `uint8 bios_sha256[32]`; HLE requires all-zero digest and external BIOS hashes the effective 16 KiB GBA BIOS image. |
| 2 | `uint32 emulation_compatibility_version`, `uint32 timing_model_flags`, `uint32 overclock_q16`, `uint32 speed_hack_mask`; timing bits 0/1 mean skip BIOS boot/request BIOS use, other bits are zero, normal overclock is `0x00010000`, and schema-v1 speed-hack mask is zero. |
| 3 | `uint8 idle_policy` (`0 = none/ignore`, `1 = remove`, `2 = detect`), `uint8 reserved[3]`. |
| 4 | `uint8 allow_opposing_directions`, `uint8 reserved[3]`. |
| 5 | `uint32 rtc_normalization_policy_version`, `uint32 fake_epoch_arithmetic_version`, `uint32 rtc_semantics_model_version`, `uint32 reserved`; all three schema-v1 versions are 1. |
| 6 | `uint8 cheats_enabled`, `uint8 reserved[3]`; admission requires false. |
| 7 | `uint32 authoritative_input_format_version`, `uint32 reserved`, `uint64 cartridge_required_input_mask`; schema-v1 format is 1. |

All multi-byte fields SHALL be little-endian. Booleans SHALL be one byte and exactly zero or one. The external-input bits SHALL be `0 = digital GBA keys`, `1 = tilt`, `2 = gyroscope`, `3 = luminance/solar`, `4 = camera`, and `5 = microphone`; bits 6–63 are reserved and SHALL reject when required.

#### Scenario: Equivalent deterministic configuration is accepted
- **WHEN** both peers have canonically identical values in every future-affecting profile category
- **THEN** profile negotiation succeeds and attachment may proceed to latency calibration

#### Scenario: Category mismatch fails before capture
- **WHEN** the peers differ in BIOS identity, timing policy, idle optimization, input filtering, RTC normalization/arithmetic/semantics-model policy, cheat state, input-format version, or cartridge-required authoritative input mask
- **THEN** the provisional session fails before calibration acceptance or replica capture and identifies the first mismatching category

#### Scenario: Different player-owned RTC sources are supported
- **WHEN** P0 uses a supported default wall-clock source and P1 uses a supported fixed source while both peers implement the same normalization policy
- **THEN** profile negotiation succeeds and each source is validated and normalized as player-owned replica state

#### Scenario: Harmless endpoint differences do not reject
- **WHEN** peers differ only in Android ABI, compiler identity, presentation options, controller mapping, paths, audio volume, or save contents
- **THEN** those differences do not alter the deterministic profile result

#### Scenario: Encoding is host-structure independent
- **WHEN** the same profile inputs are encoded on supported platforms with different native alignment or pointer widths
- **THEN** the canonical bytes and category digests are identical

#### Scenario: Unknown category policy is explicit
- **WHEN** a profile includes an unknown optional category and no unknown required category
- **THEN** compatibility ignores the optional category for equality while retaining bounded diagnostic identity

#### Scenario: Required or duplicate category fails
- **WHEN** a profile contains zero or more than sixteen records, or contains an unknown required, duplicate, out-of-order, noncanonical-Boolean, nonzero-reserved, unknown-enum, unknown-bit, or unknown-flag record
- **THEN** profile decoding or compatibility fails before calibration

#### Scenario: Canonical category layout has stable golden bytes
- **WHEN** each schema-v1 category is encoded from the same semantic values on different supported ABIs
- **THEN** its exact payload bytes, SHA-256 domain input, and 32-byte digest match the frozen golden vector

### Requirement: Local capabilities are negotiated separately from peer-equal policy
After the profile records, each `HELLO` SHALL carry little-endian `uint32 supported_rtc_source_mask`, little-endian `uint32 time_semantics_capability_mask`, little-endian `uint32 authoritative_player_rtc_source`, one-byte Boolean `content_requires_rtc`, three zero reserved bytes, and little-endian `uint64 synchronized_input_capability_mask`. RTC-source bits SHALL be `0 = RTC_NO_OVERRIDE`, `1 = RTC_FIXED`, `2 = RTC_FAKE_EPOCH`, and `3 = RTC_WALLCLOCK_OFFSET`. Time-semantics bit zero SHALL be `SIGNED_64BIT_TIME_T_V1`. Other schema-v1 bits SHALL be reserved. The synchronized-input mask SHALL use the canonical external-input bits.

Peer-equal policy categories and the two `content_requires_rtc` values SHALL match exactly. If content requires RTC, both endpoints SHALL advertise `SIGNED_64BIT_TIME_T_V1`. The intersection of supported RTC-source masks SHALL contain the authoritative P0 and P1 source types. The peer-equal cartridge-required input mask SHALL be a subset of both synchronized-input capability masks. Extra unused supported capabilities SHALL NOT reject compatibility.

#### Scenario: Non-RTC content tolerates different time capabilities
- **WHEN** non-RTC content connects one endpoint that supports `SIGNED_64BIT_TIME_T_V1` to one that does not while peer-equal policy matches
- **THEN** the time-capability difference alone does not reject the session

#### Scenario: RTC content rejects the same capability pair
- **WHEN** RTC-bearing content connects one signed-64-bit-capable endpoint to one endpoint without that capability
- **THEN** attachment fails before calibration or replica exchange with `RTC_TIME_SEMANTICS_MISMATCH`

#### Scenario: Extra RTC source capability is harmless
- **WHEN** the peers advertise different supported RTC-source masks but their intersection contains the actual P0 and P1 source types
- **THEN** source-capability negotiation succeeds when RTC policy versions also match

#### Scenario: Actual RTC source must be common
- **WHEN** either authoritative player's RTC source is absent from the peers' supported-source intersection
- **THEN** attachment fails before calibration or replica exchange and identifies that source

#### Scenario: Extra synchronized input capability is harmless
- **WHEN** one endpoint supports an additional unused synchronized-input capability and the required mask is a subset of both endpoint masks
- **THEN** external-input capability negotiation succeeds

#### Scenario: Required synchronized input must be bilateral
- **WHEN** a cartridge-required input bit is absent from either endpoint's synchronized-input capability mask
- **THEN** attachment fails before replica capture and names the missing capability

### Requirement: Determinism profile precedes mutable session exchange
Both peers SHALL exchange and validate their complete profile during bilateral `HELLO`. No endpoint SHALL send `ACCEPT`, calibration probes, replica manifests, replica chunks, or readiness messages until exact profile compatibility and required capabilities have been established.

#### Scenario: Mismatch sends no mutable state
- **WHEN** a remote `HELLO` contains an incompatible determinism profile
- **THEN** the session terminates without capturing a replica or sending any machine or save payload

#### Scenario: Duplicate compatible HELLO is idempotent
- **WHEN** the next valid global packet sequence repeats the immediately previous retained compatible `HELLO` payload byte-for-byte before phase advancement
- **THEN** it allocates no nonce, does not restart profile validation, refresh a deadline, emit a second `CALIBRATION_BEGIN`, or advance the phase twice

#### Scenario: Literal HELLO replay remains a sequence failure
- **WHEN** a byte-identical `HELLO` reuses an old global packet sequence
- **THEN** strict sequence validation rejects it before profile or phase dispatch

#### Scenario: Conflicting duplicate HELLO fails closed
- **WHEN** a second `HELLO` for the same transport generation changes any nonce, profile field, record or optional-category set, category digest, or capability field
- **THEN** the provisional session fails as a conflicting duplicate

### Requirement: Enabled cheats are incompatible with replicated play
The adapter SHALL refuse protocol-v2 attachment when any cheat is enabled and SHALL reject attempts to enable, disable, add, remove, or mutate cheats while any protocol-v2 transport or session state is non-disconnected.

#### Scenario: Enabled cheat blocks HELLO
- **WHEN** a peer attempts to start protocol v2 with an enabled cheat
- **THEN** it reports an actionable cheat-policy error and emits no compatible `HELLO`

#### Scenario: Cheat mutation during a live state is rejected
- **WHEN** a cheat API mutation is requested during transport startup, calibration, replica exchange, readiness, active play, failure teardown, or another non-disconnected state
- **THEN** the mutation is rejected without changing the active emulation state

### Requirement: Corresponding replicas use deterministic RTC time
For every logical player, corresponding replicas on both endpoints SHALL return the same RTC observations for the same emulated state. An RTC-bearing cartridge SHALL require the negotiated `SIGNED_64BIT_TIME_T_V1` capability from both endpoints, meaning both provide signed integer `time_t` semantics at least 64 bits wide; a narrower endpoint SHALL reject before calibration or replica exchange. Non-RTC cartridges SHALL not require that capability.

`RTC_NO_OVERRIDE` and `RTC_WALLCLOCK_OFFSET` sources SHALL be sampled once by that player's authoritative source at capture and normalized to an `RTC_FAKE_EPOCH` that advances from emulated time. Given sampled whole Unix seconds `S`, non-negative frame counter `F`, cycles per frame `C`, and frequency `H`, normalization SHALL calculate `elapsed_ms = floor((F × C × 1000) / H)` and `fake_epoch_value_ms = (S × 1000) - elapsed_ms` with checked signed arithmetic and an intermediate wide enough to prevent overflow. The stored result and `fake_epoch_value_ms + floor((UINT32_MAX × C × 1000) / H)` SHALL both fit signed 64-bit, covering the entire 32-bit frame-counter domain. `RTC_FIXED` and an existing valid `RTC_FAKE_EPOCH` SHALL satisfy the same common output representation. Unsupported custom RTC sources SHALL be rejected before replica exchange.

#### Scenario: Default wall clocks are normalized per logical player
- **WHEN** P0 or P1 uses the ordinary endpoint wall clock at attachment
- **THEN** its bundle records one deterministic fake epoch and both installed copies of that logical player return identical time as their equal frame counters advance

#### Scenario: Separate cartridges may retain separate epochs
- **WHEN** P0 and P1 contribute different effective RTC values
- **THEN** both endpoints install the P0 value for P0 and the P1 value for P1 without requiring the two logical cartridges to share one clock value

#### Scenario: Redistributable fixture crosses RTC seconds deterministically
- **WHEN** the RTC fixture repeatedly reads P0 and P1 across several emulated second boundaries
- **THEN** corresponding copies of each logical player return matching observations and teardown restores that player's selected original-source semantics

#### Scenario: Fixed RTC remains fixed
- **WHEN** an authoritative source uses `RTC_FIXED`
- **THEN** both corresponding replicas retain the same fixed value throughout the session

#### Scenario: Custom RTC source is rejected
- **WHEN** either authoritative source requires a custom RTC implementation not represented by the negotiated deterministic policy
- **THEN** attachment fails before replica exchange with an RTC capability diagnostic

#### Scenario: Negative Unix time uses checked signed arithmetic
- **WHEN** a supported source returns a negative representable whole-second Unix value
- **THEN** fake-epoch conversion preserves that exact second at the capture frame without unsigned wrap or implementation-defined overflow

#### Scenario: RTC conversion overflow fails before payload
- **WHEN** frame, cycle, frequency, sampled-time, multiplication, subtraction, stored-value, or target-`time_t` representation cannot satisfy the canonical arithmetic
- **THEN** attachment fails before a replica manifest or chunk is emitted

#### Scenario: Both endpoints provide signed 64-bit time semantics
- **WHEN** an RTC-bearing cartridge runs between endpoints that both advertise and implement `SIGNED_64BIT_TIME_T_V1` and its full-frame-domain value fits
- **THEN** either player's supported RTC source can be reproduced by both endpoints

#### Scenario: Only authoritative endpoint can represent RTC
- **WHEN** the authoritative endpoint can represent its RTC output but the receiving endpoint lacks signed-at-least-64-bit time semantics
- **THEN** bilateral HELLO compatibility fails before calibration or source sampling

#### Scenario: Only receiving endpoint can represent RTC
- **WHEN** the receiving endpoint supports signed 64-bit time but the authoritative endpoint does not
- **THEN** bilateral HELLO compatibility fails before calibration or source sampling

#### Scenario: Initial time fits but frame-domain horizon does not
- **WHEN** the normalized attachment value fits but advancing through the complete 32-bit frame-counter domain could overflow the signed 64-bit fake-epoch expression
- **THEN** attachment fails before replica exchange

#### Scenario: Non-RTC content remains portable
- **WHEN** a cartridge has no RTC hardware and one endpoint lacks the signed-64-bit time capability
- **THEN** RTC time semantics alone do not reject the session

### Requirement: RTC normalization is session-scoped and transactional
RTC normalization SHALL be captured with machine state, cartridge RTC metadata, save bytes, frame, and generation in every attachment and verified checkpoint. Teardown SHALL restore a complete accepted machine/save/cartridge-RTC checkpoint and then restore original RTC source semantics according to this table: `RTC_FIXED` restores the accepted fixed source/value exactly; `RTC_FAKE_EPOCH` restores the accepted epoch/frame relationship exactly; `RTC_WALLCLOCK_OFFSET` restores the original offset policy/value and may therefore jump relative to a rolled-back checkpoint; `RTC_NO_OVERRIDE` restores ordinary wall-clock policy and may therefore jump relative to a rolled-back checkpoint; custom sources are never admitted. Original-source fidelity SHALL take priority over exact checkpoint-time continuity for wall-clock-backed sources. Teardown SHALL NOT leave a partially normalized source or persist a configuration change.

#### Scenario: Failure before first verification restores attachment RTC state
- **WHEN** the session fails after pair installation but before the first periodic verification succeeds
- **THEN** the local core resumes from the attachment checkpoint with matching RTC metadata and its frontend policy restored

#### Scenario: Verified teardown restores matching RTC and machine state
- **WHEN** later verification succeeds for a fixed or fake-epoch source and the session subsequently tears down
- **THEN** the selected verified machine, save, cartridge RTC metadata, source type, and source value are restored atomically and exactly

#### Scenario: Default wall-clock teardown restores source semantics
- **WHEN** the original source was `RTC_NO_OVERRIDE` and teardown rolls back to an accepted checkpoint
- **THEN** the source returns to ordinary wall-clock behavior and any difference from the checkpoint's normalized time is documented rather than disguised as exact continuity

#### Scenario: Wall-clock-offset teardown restores original offset
- **WHEN** the original source was `RTC_WALLCLOCK_OFFSET` and teardown rolls back to an accepted checkpoint
- **THEN** the source returns to the original offset policy/value and may differ from the checkpoint's normalized effective time

#### Scenario: Failed checkpoint replacement retains the prior RTC checkpoint
- **WHEN** allocation or capture fails while replacing an accepted checkpoint
- **THEN** the previous complete machine, save, and RTC checkpoint remains the only rollback target

### Requirement: Unsynchronized external inputs fail closed
The adapter SHALL derive the peer-equal cartridge-required external-input mask from effective cartridge hardware before `HELLO`, and each endpoint SHALL separately advertise its synchronized-input capability mask. Protocol v2 SHALL require the required mask to be a subset of both endpoint masks and SHALL reject a session requiring tilt, gyroscope, luminance/solar, camera, microphone, or another input not carried authoritatively by both endpoints under the negotiated frame-input format. `HW_EREADER` SHALL be rejected explicitly as unsupported cartridge-data input before a compatible `HELLO`, calibration, or replica capture rather than being admitted as digital-only content. An endpoint's unused capability superset SHALL NOT reject. Local-output capabilities such as rumble SHALL NOT cause rejection. Current camera and microphone inputs have no equivalent detected GBA cartridge-hardware flag in this adapter path; reserved mask bits remain unavailable and any future detectable requirement SHALL fail unless carried authoritatively.

#### Scenario: Digital-only cartridge proceeds
- **WHEN** the cartridge requires only the synchronized digital GBA keys
- **THEN** the external-input capability check passes

#### Scenario: Physical sensor cartridge is rejected
- **WHEN** the effective cartridge configuration requires tilt, gyro, or a physical luminance sample
- **THEN** attachment fails before replica capture and names the unsupported input category

#### Scenario: Manual solar control is still unsynchronized
- **WHEN** a solar cartridge uses frontend button-controlled luminance instead of a physical light sensor
- **THEN** attachment is rejected because the resulting luminance value is absent from authoritative frame input

#### Scenario: Rumble remains local output
- **WHEN** a cartridge produces rumble but requires no unsupported input source
- **THEN** rumble does not prevent attachment and remains visible only from the locally owned player

#### Scenario: e-Reader cartridge data is rejected before HELLO
- **WHEN** effective cartridge hardware contains `HW_EREADER`, alone or combined with `HW_RUMBLE`
- **THEN** attachment fails with an actionable unsupported cartridge-data diagnostic before compatible `HELLO`, calibration, replica capture, or manifest transmission

### Requirement: Live deterministic settings are immutable
Every setting represented by the deterministic profile, RTC normalization policy, or authoritative-input capability negotiation SHALL be frozen for every non-disconnected protocol-v2 state. A requested change SHALL be rejected, or the session SHALL be fully torn down before the change takes effect.

#### Scenario: Core variable update during calibration is rejected
- **WHEN** a timing, idle, input-direction, RTC, or sensor-policy variable update is reported during latency calibration
- **THEN** the active value remains unchanged and the update does not affect the provisional session

#### Scenario: Setting changes after teardown
- **WHEN** the session has returned completely to `DISCONNECTED`
- **THEN** ordinary core configuration changes may take effect through the normal frontend path

### Requirement: Compatibility and diagnostics are versioned
The determinism-profile schema, external-input capability mask, RTC normalization policy, and protocol-v2 runtime compatibility SHALL be explicitly versioned. Unsupported versions and older peers SHALL reject one another during `HELLO` without automatic downgrade. Diagnostics SHALL retain and identify the first stable profile category, capability mismatch class, missing required-input mask, and local/remote schema or policy versions without logging profile digests, paths, BIOS data, save contents, raw input history, or commercial content.

#### Scenario: Older experimental peer is rejected
- **WHEN** a peer with the pre-hardening protocol-v2 runtime attempts to connect
- **THEN** both endpoints reject the incompatible runtime before replica capture

#### Scenario: Diagnostic identifies policy category safely
- **WHEN** compatibility fails in one deterministic category
- **THEN** the log names that category and the involved schema versions without exposing sensitive payload data
