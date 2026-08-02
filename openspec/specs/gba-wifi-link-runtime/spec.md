# GBA Wi-Fi Link Runtime Specification

## Purpose

Define the canonical GBA Wi-Fi Link product façade, its optional ownership of ordinary emulation, the preserved versioned session/wire boundary, stable diagnostics, permanent validation, and current-versus-historical documentation rules.

## Requirements

### Requirement: GBA Wi-Fi Link is the canonical product runtime
The libretro core SHALL present the replicated GBA Wi-Fi Link as one canonical product rather than as a selectable or alternative v2 product path. A product-specific `gba-wifi-link` / `mLibretroGBAWifiLink*` façade SHALL own the frontend lifecycle and the sole GBA Netpacket registration path. The concrete `session-v2` state machine and `protocol-v2` codec/schema SHALL retain version-qualified identities because they implement the actual version-2 compatibility contract.

#### Scenario: Supported GBA content registers Netpacket
- **WHEN** supported GBA content loads and RetroArch provides Netpacket
- **THEN** the core registers the canonical GBA Wi-Fi Link adapter without selecting, naming, or invoking an alternative runtime

#### Scenario: Developer follows the active code path
- **WHEN** a developer follows registration from libretro through attachment, session, replicated execution, presentation, teardown, and tests
- **THEN** the canonical GBA Wi-Fi Link façade leads directly into the explicitly versioned session and codec without an empty product-level v1/v2 selection branch

#### Scenario: Stale runtime option remains on disk
- **WHEN** an existing configuration retains `mgba_gba_link_netplay_runtime` with any value
- **THEN** the key remains undeclared, unqueried, and behaviorally inert

### Requirement: Canonical names do not reuse retired protocol-v1 identities
A canonical rename target SHALL NOT reuse a path, symbol family, test target, or configuration identity listed in the protocol-v1 retirement inventory. The retired `session.[ch]`, `netpacket.[ch]`, `GBALinkSession*`, `GBA_LINK_SESSION_*`, `mLibretroNetpacket*`, `gba-netplay-session`, and `libretro-netpacket` identities SHALL remain historical and SHALL NOT name the current façade, versioned session, or permanent tests.

#### Scenario: Product façade receives its permanent name
- **WHEN** `netpacket-v2.[ch]` and its libretro entry points are canonicalized
- **THEN** they become product-specific `gba-wifi-link.[ch]` and `mLibretroGBAWifiLink*` surfaces rather than any retired protocol-v1 identity

#### Scenario: Versioned session is integrated behind the façade
- **WHEN** the sole current session implementation is referenced by production code or tests
- **THEN** `session-v2.[ch]`, `GBALinkV2Session*`, `GBA_LINK_V2_SESSION_*`, and the versioned session target retain unambiguous compatibility identities

#### Scenario: Permanent test targets are renamed
- **WHEN** adapter and replay targets lose obsolete product-branch wording
- **THEN** they use `test-libretro-gba-wifi-link` and `test-libretro-gba-wifi-link-replay` or equally product-specific non-retired names while the versioned session target remains versioned

### Requirement: GBA Wi-Fi Link is optional to ordinary emulation
The canonical GBA Wi-Fi Link façade SHALL own guest execution only while its GBA link session is live. Absence of Netpacket support, absence of an admitted peer, failed registration, or complete teardown SHALL leave ordinary single-core execution unchanged. The GBA-specific façade SHALL NOT register for or own GB, GBC, or another non-GBA runtime.

#### Scenario: RetroArch does not provide Netpacket
- **WHEN** supported GBA content loads without the Netpacket interface
- **THEN** the content loads and executes through ordinary single-core input, SIO, audio, video, state, cheat, reset, and unload behavior

#### Scenario: Netpacket exists without an admitted peer
- **WHEN** the interface is available but no peer has been admitted
- **THEN** the original GBA core retains ordinary disconnected execution and lifecycle ownership

#### Scenario: Live session tears down
- **WHEN** a live GBA Wi-Fi Link session completes bounded teardown
- **THEN** the accepted local checkpoint is restored as required and ownership returns to the ordinary disconnected execution model

#### Scenario: Non-GBA content loads
- **WHEN** GB, GBC, or another non-GBA core path loads
- **THEN** no GBA Wi-Fi Link registration or execution ownership is introduced

### Requirement: The version-2 wire contract remains explicit and unchanged
The canonical runtime SHALL retain the version-qualified codec/schema boundary for the actual wire contract. Protocol name `mgba-gba-link-replicated-v2`, magic `GLR2`, protocol version 2, runtime compatibility version, header and payload layouts, message/reason/capability/policy numeric values, deterministic-profile and calibration digest domains, encoded golden vectors, and legacy-packet rejection semantics SHALL remain unchanged by runtime integration.

#### Scenario: Versioned session encodes a packet
- **WHEN** the versioned session sends any attachment, calibration, replica, input, verification, or teardown message through the canonical product façade
- **THEN** the version-2 codec produces the same bytes and numeric values as the pre-integration baseline

#### Scenario: Older or malformed peer sends incompatible bytes
- **WHEN** legacy v1, incompatible version, malformed, or wrong-runtime bytes reach the canonical runtime
- **THEN** the version-2 decoder rejects them at the same boundary and ordinary bounded recovery remains unchanged

#### Scenario: Wire-facing source is inspected
- **WHEN** a developer inspects packet structures, encoded enums, protocol constants, or canonical digest inputs
- **THEN** their version-qualified names make the compatibility boundary explicit rather than being normalized as product-only naming

### Requirement: Permanent validation surfaces use permanent-purpose ownership
Every active test harness, executable target, helper, analyzer, configuration path, and CI job SHALL be named for the current invariant, product surface, or real compatibility layer it verifies. An active product surface SHALL NOT retain `spike`, `v2-only`, runtime-selection, obsolete feature-branch, or retirement-only naming solely because of its development history. Versioned session, codec, schema, digest, golden-vector, and compatibility tests SHALL retain version identity where it is semantically real. Before a prototype-named harness is renamed or removed, each distinct invariant it uniquely owns SHALL have an explicit permanent test owner.

#### Scenario: Prototype harness owns unique coverage
- **WHEN** an active spike-named harness uniquely verifies a scheduler, threading, frontend, or lifecycle invariant
- **THEN** it is renamed for that invariant and remains in the protected regression matrix

#### Scenario: Prototype harness is fully superseded
- **WHEN** production pair, adapter, session, or replay tests already own every distinct invariant in an active spike-named harness
- **THEN** the redundant harness is removed while its historical report remains available

#### Scenario: Current qualification assets are used
- **WHEN** automation or a developer follows active qualification commands
- **THEN** paths and target names identify the current GBA Wi-Fi Link workflow without referring to an obsolete spike or alternative runtime branch

### Requirement: Removal-phase controls yield to positive runtime guarantees
Retirement-only source/path absence auditing SHALL be removed from the permanent protected matrix after every enduring guarantee has a positive current owner. Direct registration, stale-option inertness, wire identity, legacy rejection, callback lifecycle, and runtime behavior SHALL remain covered by ordinary façade, codec, session, replay, or build tests. A mandatory lightweight positive boundary audit SHALL verify the current product/session/wire architecture and SHALL NOT merely rename the deleted-protocol audit.

#### Scenario: Retirement audit assertion remains valuable
- **WHEN** an assertion in the protocol-v1 absence audit protects a current runtime invariant
- **THEN** that assertion is migrated to a named positive test or build inspection before the retirement audit is deleted

#### Scenario: Assertion only proves deleted paths remain deleted
- **WHEN** an audit assertion has no current behavior beyond repeating the completed removal inventory
- **THEN** archived PR evidence and Git history retain that fact without running it in every future sanitizer job

#### Scenario: Positive boundary audit examines active source and targets
- **WHEN** the fixture/tooling CI job validates the current runtime boundary
- **THEN** it proves the canonical façade is the sole libretro GBA Netpacket registration path, the stale selector is absent, current product names are canonical, version-qualified names are allow-listed to real compatibility or historical zones, and no canonical target reuses a retired v1 identity

#### Scenario: Positive boundary audit examines Android binary
- **WHEN** the Android ARM64 build job inspects the shared object
- **THEN** it proves that `mgba-gba-link-replicated-v2` and the canonical GBA Wi-Fi Link diagnostic identity are present while the session and protocol compatibility boundary remains versioned

#### Scenario: Protected matrix runs after integration
- **WHEN** the canonical runtime is ready to merge
- **THEN** focused normal, ASan/UBSan, TSan, complete mGBA, fixture/helper/analyzer, paired adapter replay, and Android ARM64 gates pass under current-purpose names

### Requirement: Qualification diagnostics have a stable machine-readable contract
Human-readable diagnostic prefixes and explanatory prose MAY use canonical GBA Wi-Fi Link wording. Machine-readable qualification record kinds, diagnostic schema version, role and session-correlation fields, units, field meanings, and privacy guarantees SHALL remain stable; if that contract changes, its schema version and all validators, analyzers, fixtures, and current documentation SHALL change together. Qualification tools SHALL NOT infer structured state from changeable human prose.

Diagnostic schema 1 SHALL emit a successful registration record with kind `product`, `schema=1`, product identity `mgba-gba-wifi-link`, and protocol identity `mgba-gba-link-replicated-v2`. Every runtime failure SHALL emit a separate record with kind `failure`, `schema=1`, endpoint role, accepted or provisional session identity where available, calibration/session generation where available, numeric failure reason, stable state identity, and replicated frame. An unavailable session identity or generation SHALL be encoded as zero. Structured records SHALL NOT contain a path, address, ROM identity or contents, save data, input history, profile digest, BIOS data, or another sensitive value.

#### Scenario: Human-facing attachment wording changes
- **WHEN** an attachment, calibration, latency, or teardown message receives canonical product wording
- **THEN** its structured record kind, role, provisional session, generation, policy, delay, wait units, and privacy behavior remain machine-readable and correlated

#### Scenario: Structured diagnostic contract must change
- **WHEN** a field, unit, record kind, or correlation rule cannot remain compatible
- **THEN** the implementation increments an explicit diagnostic schema version and updates every producer, consumer, fixture, and instruction in the same reviewed change

#### Scenario: Qualification evidence is analyzed
- **WHEN** a validator or analyzer processes a current runtime log
- **THEN** it uses structured fields and the supported diagnostic schema rather than matching product prose for semantic state

#### Scenario: Human failure prose changes
- **WHEN** explanatory registration or failure wording changes without changing the schema-1 records
- **THEN** qualification accepts or rejects from the structured records exactly as before

#### Scenario: Structured failure is present
- **WHEN** a supported schema-1 `failure` record appears anywhere in the analyzed run
- **THEN** qualification rejects the run even when all human-facing messages appear successful

#### Scenario: Structured registration is absent or malformed
- **WHEN** the required schema-1 `product` record is missing, malformed, or names an unexpected product or protocol identity
- **THEN** qualification fails closed rather than inferring registration from human prose

#### Scenario: Failure correlation is inconsistent
- **WHEN** a structured failure role, session identity, or generation conflicts with the endpoint's accepted structured attachment evidence
- **THEN** qualification rejects the evidence as mixed-session or mixed-role data

### Requirement: Current instructions and historical evidence are distinct
README, operating guidance, roadmap, active validation commands, qualification tooling, CI, provenance instructions, and authoritative current capabilities SHALL describe the canonical GBA Wi-Fi Link runtime. Technical wire documentation SHALL identify protocol version 2 where required. Archived OpenSpec, protocol-v1 retirement material, feasibility/spike reports, historical branches, artifact hashes, and measured v1/v2 comparisons SHALL remain factually unchanged or explicitly labelled as historical and SHALL NOT become current setup instructions.

#### Scenario: User reads current setup guidance
- **WHEN** a user installs, hosts, joins, changes latency policy, troubleshoots, or reports a current build
- **THEN** the instructions describe GBA Wi-Fi Link directly and do not ask the user to understand a deleted runtime comparison

#### Scenario: Developer reads the wire reference
- **WHEN** a developer needs encoded compatibility details
- **THEN** the version-2 protocol name, magic, versions, fields, values, and digest domains remain explicitly documented

#### Scenario: Developer reads historical evidence
- **WHEN** a developer studies the retired cable-sync architecture or the replicated-pair feasibility work
- **THEN** original branch, target, v1/v2, spike, hash, and measurement context remains available with a clear historical status

### Requirement: Runtime integration is behavior-neutral
Canonical naming and ownership changes SHALL NOT alter calibration, selected delay, authoritative input mapping, RTC normalization, replica content, pair scheduling, local cable transactions, save/checkpoint ownership, verification, presentation, audio, lifecycle guards, failure reasons, or teardown outcomes. Physical commercial replay SHALL be required only if implementation review or automated evidence finds an actual production behavior change.

#### Scenario: Paired replay runs before and after integration
- **WHEN** identical fixtures and transport schedules exercise the old-named and canonical runtime heads
- **THEN** wire bytes, selected delays, state traces, frame/input ownership, cable counters, audio accounting, checkpoints, and teardown outcomes match

#### Scenario: Rename-only change reaches Android build
- **WHEN** the Android ARM64 shared object is built after integration
- **THEN** it exposes the same version-2 Netpacket identity and gameplay behavior through canonical current diagnostics

#### Scenario: Review finds a behavior difference
- **WHEN** golden, replay, sanitizer, complete-suite, helper, analyzer, or binary inspection evidence changes beyond approved naming
- **THEN** landing stops and the behavior difference is either removed or separately specified and reviewed before physical qualification
