## MODIFIED Requirements

### Requirement: Enabled cheats are incompatible with replicated play
The GBA Wi-Fi Link adapter SHALL refuse attachment when any cheat is enabled and SHALL reject attempts to enable, disable, add, remove, or mutate cheats while any link transport or session state is non-disconnected.

#### Scenario: Enabled cheat blocks HELLO
- **WHEN** a peer attempts to start GBA Wi-Fi Link with an enabled cheat
- **THEN** it reports an actionable cheat-policy error and emits no compatible `HELLO`

#### Scenario: Cheat mutation during a live state is rejected
- **WHEN** a cheat API mutation is requested during transport startup, calibration, replica exchange, readiness, active play, failure teardown, or another non-disconnected state
- **THEN** the mutation is rejected without changing the active emulation state

### Requirement: Unsynchronized external inputs fail closed
The GBA Wi-Fi Link adapter SHALL derive the peer-equal cartridge-required external-input mask from effective cartridge hardware before `HELLO`, and each endpoint SHALL separately advertise its synchronized-input capability mask. The current version-2 wire/runtime contract SHALL require the required mask to be a subset of both endpoint masks and SHALL reject a session requiring tilt, gyroscope, luminance/solar, camera, microphone, or another input not carried authoritatively by both endpoints under the negotiated frame-input format. `HW_EREADER` SHALL be rejected explicitly as unsupported cartridge-data input before a compatible `HELLO`, calibration, or replica capture rather than being admitted as digital-only content. An endpoint's unused capability superset SHALL NOT reject. Local-output capabilities such as rumble SHALL NOT cause rejection. Current camera and microphone inputs have no equivalent detected GBA cartridge-hardware flag in this adapter path; reserved mask bits remain unavailable and any future detectable requirement SHALL fail unless carried authoritatively.

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
Every setting represented by the deterministic profile, RTC normalization policy, or authoritative-input capability negotiation SHALL be frozen for every non-disconnected GBA Wi-Fi Link state. A requested change SHALL be rejected, or the session SHALL be fully torn down before the change takes effect.

#### Scenario: Core variable update during calibration is rejected
- **WHEN** a timing, idle, input-direction, RTC, or sensor-policy variable update is reported during latency calibration
- **THEN** the active value remains unchanged and the update does not affect the provisional session

#### Scenario: Setting changes after teardown
- **WHEN** the session has returned completely to `DISCONNECTED`
- **THEN** ordinary core configuration changes may take effect through the normal frontend path
