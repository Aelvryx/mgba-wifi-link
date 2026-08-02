## ADDED Requirements

### Requirement: Replicated protocol v2 is the sole shipped runtime
The libretro core SHALL expose and register exactly one GBA Wi-Fi link runtime: replicated protocol v2. It SHALL NOT expose a runtime-selection core option, query `mgba_gba_link_netplay_runtime`, or branch GBA execution and lifecycle behaviour according to a retained protocol-v1 setting.

#### Scenario: GBA content loads without a runtime selector
- **WHEN** supported GBA content is loaded and RetroArch provides the Netpacket interface
- **THEN** the core registers replicated protocol v2 directly and exposes no GBA link runtime-selection option

#### Scenario: Stale runtime-selection configuration remains on disk
- **WHEN** a user's RetroArch options file still contains `mgba_gba_link_netplay_runtime` with `cable-v1`, `replicated-v2`, or any other value
- **THEN** the core neither declares nor queries the key, its value has no effect, and any GBA Netpacket session still follows protocol v2

#### Scenario: Ordinary non-networked execution continues
- **WHEN** no Netpacket peer is admitted
- **THEN** the original single GBA core executes with the same local SIO, input, state, cheat, reset, unload, audio, and video behaviour as before removal

### Requirement: Retired protocol v1 cannot execute or negotiate
Shipped sources, active tools, CI, and build targets SHALL contain no protocol-v1 distributed-SIO adapter, session, timeline, network SIO driver, packet codec, compatibility string, runtime entry point, packet-log analyzer, analyzer self-test, v1-only spike configuration, or dedicated v1 test executable. A packet carrying retired v1 magic, version, or message bytes SHALL fail protocol-v2 decoding before message dispatch and before any payload from those bytes is interpreted or applied to protocol, replica, or emulated state. Protocol v2 SHALL NOT invoke a v1 decoder, negotiate v1, downgrade, or select a fallback runtime.

#### Scenario: Legacy packet arrives during attachment
- **WHEN** a peer sends a bounded raw fixture containing retired v1 magic, version, or distributed-SIO message bytes before replica capture
- **THEN** protocol-v2 decoding rejects it before dispatch, zero replicas or manifests are captured or transmitted, and no v1 decoder or runtime is invoked

#### Scenario: Legacy packet arrives after readiness
- **WHEN** the bounded raw legacy fixture arrives after the protocol-v2 pair is ready and has advanced emulated state
- **THEN** protocol-v2 decoding rejects it before runtime dispatch or application and the session performs ordinary bounded malformed-packet teardown with accepted-checkpoint restoration
- **AND** no v1 decoder, negotiation, or fallback runtime is invoked

#### Scenario: Production and test targets are enumerated
- **WHEN** the active non-archived source tree and generated build graph are inspected
- **THEN** no v1 adapter, driver, session, timeline, codec, compatibility name, analyzer/tooling path, active CI invocation, or dedicated v1 test target is present

#### Scenario: Protocol-v2 wire compatibility is checked
- **WHEN** the surviving protocol-v2 golden codec and paired-adapter replay tests run after removal
- **THEN** their encoded packets, compatibility version, session transitions, and replicated output remain unchanged

### Requirement: Shared invariants survive independently of v1
Before deleting a v1-only test, the project SHALL classify every test case and every distinct behavioural invariant it uniquely covers as retired v1 behaviour, generic common-SIO/local-lockstep behaviour, or transport/session/frontend behaviour still required by v2. Individual assertion statements that jointly prove one invariant SHALL NOT require separate inventory entries. Every non-v1 invariant SHALL have an explicit surviving test owner, and the removal SHALL preserve generation-safe callbacks, bounded queues, state-operation guards, reset/unload safety, common SIO completion and IRQ behaviour, local lockstep topology, and protocol-v2 teardown coverage.

#### Scenario: A v1 test contains a distinct generic invariant
- **WHEN** the removal inventory finds a test case whose distinct invariant specifies common SIO, local lockstep, or v2-used lifecycle behaviour rather than v1 wire policy
- **THEN** that invariant is already owned by or migrated to an appropriate surviving test before the v1 test target is deleted

#### Scenario: A test invariant specifies only retired behaviour
- **WHEN** a test case's distinct invariant covers grants, network mode barriers, distributed transfer commit/catch-up/decision, or another v1-only state transition
- **THEN** that test case is removed with the retired runtime and remains represented only by historical evidence where useful

#### Scenario: Protected regression matrix runs
- **WHEN** the removal is ready to merge
- **THEN** focused normal, ASan/UBSan, TSan, complete mGBA, fixture/helper reproducibility, Android ARM64, generic SIO, local lockstep, and paired protocol-v2 replay gates pass

### Requirement: Historical evidence is preserved without live instructions
Archived OpenSpec changes and historical v1 performance or validation evidence SHALL remain available and SHALL NOT be rewritten as if v1 had never existed. Current product documentation, qualification configuration, and executable validation instructions SHALL describe protocol v2 only; retained historical sections SHALL identify v1 as retired and SHALL NOT instruct users to select it in a current build.

The static absence audit SHALL cover production headers/sources, a clean configured build target list, active tests/tools, the built Android shared object's strings and symbols, and current user/developer instructions. It SHALL allow v1 references only under `openspec/changes/archive`, in clearly labelled historical evidence sections, and in one bounded raw legacy-wire fixture contained by a protocol-v2 rejection test.

#### Scenario: Reader follows current setup documentation
- **WHEN** a user follows the README or current Wi-Fi link guide
- **THEN** the documented workflow contains only protocol-v2 registration and latency-policy selection and offers no v1 runtime option

#### Scenario: Reader examines historical evidence
- **WHEN** a developer reads archived OpenSpec material or retained v1 performance measurements
- **THEN** the architectural evidence and conclusions remain available with clear retired or historical context

#### Scenario: Qualification helper validates a current build
- **WHEN** automation validates core options and runtime logs after v1 removal
- **THEN** it requires protocol-v2 identity and the selected latency policy without requiring the removed runtime-selection key

#### Scenario: Absence audit encounters an allowed historical reference
- **WHEN** the static audit finds a v1 reference inside an archived OpenSpec artifact or clearly labelled historical evidence section
- **THEN** it reports the allow-listed reference without treating it as an active runtime, test, tool, build, or instruction failure

#### Scenario: Absence audit encounters the bounded rejection fixture
- **WHEN** the static audit finds the single bounded raw legacy-wire byte fixture inside its protocol-v2 rejection test
- **THEN** it allows that data-only fixture while still requiring the v1 codec, symbols, headers, and runtime to be absent
