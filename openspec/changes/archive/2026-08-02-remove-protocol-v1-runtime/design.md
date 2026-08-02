## Context

Protocol v1 distributes individual GBA SIO events across RetroArch Netpacket. It established useful correctness and failure-semantics evidence, but its host-led grants and transfer barriers put Wi-Fi round trips in the emulated cable's critical path. Protocol v2 instead runs identical P0/P1 pairs locally and synchronizes authoritative frame inputs; it has passed the automated, Android, Mario Kart, and Four Swords qualification needed to replace v1.

The two runtimes are still vertically interleaved. Libretro exposes `mgba_gba_link_netplay_runtime`, branches through lifecycle, input, state, cheat, reset, and unload paths, and compiles both adapters. V1-specific driver, timeline, session, codec, integration, and frontend tests remain in normal and sanitizer gates. Some nominally v1 headers also contain small role, identity, decode, transport, or queue definitions consumed by v2, so deleting files by name without first separating those dependencies would either break v2 or leave a disguised v1 substrate.

Historical OpenSpec archives and performance evidence explain why the architecture changed and remain valuable. They are records, not supported runtime instructions.

## Goals / Non-Goals

**Goals:**

- Make replicated protocol v2 the only GBA Wi-Fi link runtime compiled, registered, and executable by the libretro core.
- Remove the runtime selector and make the entire stale `mgba_gba_link_netplay_runtime` key inert, regardless of its retained value.
- Delete v1-only codec, session, timeline, SIO-driver, adapter, test, and build surfaces.
- Retain only transport-neutral or v2-used definitions, under ownership that no longer implies v1 availability.
- Preserve distinct generic common-SIO and failure/lifecycle invariants independently of the removed implementation.
- Remove active v1-only packet analysis, self-test, CI, and spike-configuration tooling rather than maintaining an unsupported archived toolchain.
- Keep archived planning and historical measurements available while preventing them from reading as current operating instructions.

**Non-Goals:**

- Changing protocol-v2 wire bytes, runtime compatibility, calibration, input delay, replica scheduling, save ownership, or teardown semantics.
- Deciding whether one-frame operation should become the default or measuring direct-hotspot performance.
- Adding a replacement diagnostic runtime, compatibility downgrade, or build flag for v1.
- Rewriting archived OpenSpec artifacts or deleting historical performance evidence.
- Broad refactoring of common mGBA SIO or local lockstep beyond dependency cleanup required by removal.

## Decisions

### Remove the selector instead of hiding the v1 value

The `mgba_gba_link_netplay_runtime` core option and the `netplayV1Diagnostic` branch are removed. GBA Netpacket registration, frame execution, lifecycle guards, reset, unload, cheats, and state operations use the v2 adapter directly. A stale RetroArch configuration entry is an unknown, unqueried key and cannot alter runtime selection.

Keeping a hidden value or compile-time switch was rejected because it would preserve the branching, maintenance burden, and accidental-selection risk that this change exists to remove. A future diagnostic tool must be proposed on its own merits rather than reviving an unusable production runtime.

### Delete v1 vertically while retaining a neutral v2 substrate

The removal follows ownership, not directory names. V1 packet structures and codecs, host-led timeline, distributed session, `GBASIONetplayDriver`, libretro v1 adapter, and their dedicated tests/build targets are deleted. Definitions currently housed in v1-named headers but genuinely required by v2—such as endpoint role, content identity sizes, copied transport queues, decode status, or transport diagnostics—are moved to a neutral or explicitly v2-owned header/API before the old header is removed. Their v2-observable numeric identities remain unchanged, including host/client role values, SHA-1 identity length, copied-queue capacity, decode-status values used by v2 diagnostics, and surviving generic transport-reason values.

No v1 message type, wire constant, transfer state machine, compatibility name, or entry point is retained merely to avoid updating includes. Conversely, shared identity, transport, replica, RTC, input, and cryptographic code used by v2 is not deleted just because it resides under `sio/netplay`.

Renaming every remaining generic `GBALink*` symbol was rejected as unrelated churn. Ownership is established by dependency and absence of v1 wire/session types, not by mechanically adding `V2` to transport-neutral names.

### Preserve invariants, not the obsolete oracle

Before deleting v1-only tests, implementation maps every v1 test case and every distinct behavioural invariant it uniquely covers into three groups:

1. V1 packet/timeline/driver behaviour, which is retired with the runtime.
2. Generic common-SIO and local-lockstep behaviour, which must already exist or be migrated to `gba-sio`, replicated-pair, or another transport-independent test.
3. Generation-safe callback, bounded-queue, state-operation, reset, unload, and failure behaviour still used by v2, which must already exist or be migrated to v2 transport/session/adapter tests.

One behavioural test may use many assertion statements to prove one invariant; those statements are not separate accounting units. The implementation report records each distinct migrated invariant and its new test owner in a compact table, avoiding compliance archaeology while preventing a silent reduction in coverage.

### Preserve history but remove current instructions

Files under `openspec/changes/archive` are immutable historical planning records. Historical performance baselines and validation sections may retain v1 packet counts and conclusions, but they must be labelled as retired evidence and must not tell users how to select v1 in a current build. The active v1 packet-log analyzer, its Python self-test, protected-CI invocation, and v1-only spike configurations are deleted; their recorded results and Git history are sufficient historical evidence. Current README, user guide, protocol overview, qualification helpers/configuration, CI target lists, and active validation commands are updated to describe v2 only.

This is preferred to deleting all v1 references because the evidence explains the architectural decision and remains useful for future reviews.

### Prove deletion and v2 non-regression separately

The static absence audit covers clean searches of production headers/sources, a clean CMake configuration and generated target list, active tests and tools, Android shared-object strings/symbols, and current user/developer instructions. It proves that the core option, compatibility string, v1 adapter/driver/session/timeline symbols, v1-only sources, v1 analyzer/tooling, and v1-only test targets are absent. Its only allow-list is `openspec/changes/archive`, clearly labelled historical sections in evidence documents, and one bounded raw legacy-wire byte fixture inside a v2 rejection test.

Runtime tests prove that GBA Netpacket registration reaches v2 without a selector, every stale value of the removed configuration key is inert, and legacy wire input fails protocol-v2 decoding before dispatch or application. A legacy fixture received before replica capture produces no capture or manifest; the same fixture received after readiness triggers ordinary bounded malformed-packet teardown and accepted-checkpoint restoration. No test retains or invokes a v1 codec.

The full protected matrix—focused normal, ASan/UBSan, TSan, complete mGBA suite, fixture/helper reproducibility, and Android ARM64 build—remains the merge gate. No new commercial device run is required because the change removes an unreachable-by-default alternative and does not alter v2 behaviour; a short automated paired-v2 replay is the behavioural regression gate.

## Risks / Trade-offs

- **Shared definitions are mistaken for v1-only code** → Build a dependency inventory first, move only proven v2 dependencies, and compile protocol-v2 tests after each removal layer.
- **Deleting v1 tests removes the sole proof of a generic SIO invariant** → Classify each test case and distinct invariant before deletion and migrate transport-independent cases to an explicit surviving owner.
- **A stale RetroArch option leaves users expecting a selectable runtime** → Remove the option declaration and query; document that stale config lines are ignored and can be deleted.
- **Historical documents look like supported instructions** → Preserve measurements but label retired sections and remove live selection steps and active target names.
- **The smaller runtime loses a convenient rollback path** → Roll back by reverting the source change or installing the prior tagged alpha, not by shipping two architectures indefinitely.
- **Large deletion obscures an unintended v2 change** → Separate dependency extraction from deletion in the patch stack and require v2 golden/replay tests plus a no-wire-change review.

## Migration Plan

1. Record a source/build/test/tool ownership inventory, a test-case/distinct-invariant disposition table, and the pre-removal focused test baseline.
2. Move the small transport-neutral definitions consumed by v2 out of v1-owned headers without changing encoded v2 bytes or public frontend behaviour.
3. Make libretro registration and lifecycle paths unconditionally v2, remove the core option, and test stale-option behaviour.
4. Delete v1 adapter, driver, timeline, session, codec, dedicated tests, Python analyzer/self-test, active analyzer CI, v1-only spike configurations, and build entries; migrate any surviving generic invariants identified by the inventory.
5. Update current documentation, qualification helpers/configuration, active CI filters, and validation commands while preserving labelled archives, recorded historical evidence, and Git history.
6. Run static absence checks, focused v2/generic-SIO tests, sanitizers, the complete suite, fixture/helper checks, and Android ARM64 build.
7. Merge through the protected branch and close GitHub issue #6. Rollback, if required, is a normal revert of the removal commit or use of the previous tagged alpha.

## Open Questions

None. Any newly discovered dependency that would require changing protocol-v2 behaviour stops implementation and returns to focused design review.
