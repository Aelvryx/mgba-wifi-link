## Context

Protocol v1 was removed in PR #14, leaving one supported GBA Wi-Fi Link product. The repository nevertheless still exposes the libretro façade and current product material as a branch: `netpacket-v2`, `mLibretroNetpacketV2*`, v2-only roadmap/release language, old feature-branch CI filters, active `spike` harness names, and a current capability named `gba-link-runtime-selection`.

Not every version-qualified name is stale. `session-v2` implements the exact version-2 handshake, calibration, replica exchange, readiness, input, verification, and teardown state machine. The reliable Netpacket payload has a real compatibility identity: `mgba-gba-link-replicated-v2`, magic `GLR2`, protocol version 2, runtime compatibility version 2, fixed message/reason numbers, fixed digest domains, and golden encoded bytes. Those identifiers distinguish a concrete compatibility contract and are not aliases for a deleted runtime selector.

The integration therefore needs a hard boundary between the canonical product/runtime surface and the versioned wire/schema surface. It must also avoid rewriting historical evidence or manufacturing a protocol change from a naming cleanup.

## Goals / Non-Goals

**Goals:**

- Present one coherent product called GBA Wi-Fi Link through a canonical product-specific libretro façade and current user, test, CI, tooling, and specification surfaces.
- Remove names whose only purpose was distinguishing the product façade from deleted v1 code while retaining version names that identify the concrete session or wire contract.
- Preserve every wire-visible byte, numeric identity, digest input, compatibility value, packet-validation rule, and legacy rejection behavior.
- Rename or retire active `spike`, obsolete-branch, selection, and retirement-only scaffolding according to its current ownership.
- Keep permanent regression coverage and historical evidence without making either appear to be another selectable architecture.
- Make the rename mechanically auditable and prove that behavior is unchanged.
- Preserve ordinary disconnected execution and prevent the GBA-specific façade from claiming non-GBA runtimes.
- Preserve machine-readable diagnostic records independently from changeable human-facing wording.

**Non-Goals:**

- Changing the protocol name, magic, protocol version, runtime compatibility version, packet layout, message/reason values, digest domains, or golden fixtures.
- Supporting mixed protocol generations, downgrade, fallback, or a future protocol v3.
- Changing calibration, input delay, replicated scheduling, RTC, save/checkpoint, cable, audio/video, or teardown behavior.
- Changing the default latency policy or performing the direct-hotspot experiment.
- Erasing or rewriting archived OpenSpec, protocol-v1 retirement evidence, feasibility reports, or historical performance conclusions.
- Renaming unrelated upstream APIs, third-party versioned interfaces, ROM-database titles, or schema-v1 identifiers.
- Reusing a retired protocol-v1 path, symbol family, target, or configuration identity for a different implementation.

## Decisions

### 1. Use a three-zone naming model

Active names are classified before editing:

| Zone | Canonical treatment | Examples |
| --- | --- | --- |
| Product façade | Product-specific current-purpose name | GBA Wi-Fi Link, `gba-wifi-link.c`, `mLibretroGBAWifiLink*`, `test-libretro-gba-wifi-link` |
| Versioned session and wire/schema | Retain explicit version identity | `session-v2.c`, `GBALinkV2Session*`, `protocol-v2.c`, `GBALinkV2Packet`, `GBA_LINK_V2_MESSAGE_*`, `mgba-gba-link-replicated-v2`, digest domains |
| Historical | Preserve and label; do not make active | protocol-v1 retirement record, feasibility/spike reports, archived changes, old artifact hashes |

The product is singular; the concrete session and packet schema remain versioned. This avoids both undesirable extremes: leaving the user-facing façade named as an alternative, or pretending the version-2 state machine is a protocol-neutral abstraction.

A canonical rename target SHALL NOT reuse a path, symbol family, test target, or configuration identity listed in `docs/protocol-v1-retirement.md`. In particular, `session.[ch]`, `netpacket.[ch]`, `GBALinkSession*`, `GBA_LINK_SESSION_*`, `mLibretroNetpacket*`, `gba-netplay-session`, and `libretro-netpacket` remain retired identities. This preserves unambiguous history, blame, logs, binary inspection, and future namespace ownership.

### 2. Canonicalize the product façade; keep the session and codec versioned

The libretro adapter becomes `gba-wifi-link.[ch]` with `mLibretroGBAWifiLink*` entry points. It is the product façade for presentation, physical input, save/lifecycle ownership, qualification diagnostics, and exact Netpacket registration. Adapter and paired-replay targets become `test-libretro-gba-wifi-link` and `test-libretro-gba-wifi-link-replay`.

`session-v2.[ch]`, `GBALinkV2Session*`, `GBALinkV2SessionState`, `GBALinkV2Deadline*`, and `GBA_LINK_V2_SESSION_*` retain their current identities. Its permanent target remains `test-gba-netplay-session-v2`. `protocol-v2.[ch]` remains the codec boundary. `GBALinkV2Packet`, its header and payload structs, wire capability/message/reason/policy enums, `GBA_LINK_V2_PROTOCOL_*`, and deterministic-profile structures whose canonical digest belongs to the v2 domain retain their names and exact definitions.

The dependency direction is explicit:

```text
libretro.c
    |
    v
GBA Wi-Fi Link product façade
gba-wifi-link.[ch] / mLibretroGBAWifiLink*
    |
    v
versioned session implementation
session-v2.[ch] / GBALinkV2Session*
    |
    v
versioned codec and schema
protocol-v2.[ch] / GBALinkV2Packet*
```

Alternative considered: mechanically remove every `V2` token. Rejected because many such tokens name exact encoded values and digest domains, and because a mass rename would obscure the compatibility boundary rather than clarify it.

### 3. Give permanent tests permanent-purpose names

The session and codec targets retain `-v2` because they verify a versioned compatibility implementation. Product-façade and paired-replay targets use new GBA Wi-Fi Link names rather than either v2 branch names or the retired v1 target identities.

Every active `spike` source, target, configuration, and CI entry is inventoried by distinct invariant:

- If the invariant is still uniquely valuable, rename the harness after that invariant, such as replicated-pair scheduler comparison or local pair frontend integration.
- If production pair, adapter, or replay tests already own the invariant, delete the superseded spike harness and retain its historical report.
- Historical documents may keep their original spike terminology and target names.

This decision avoids preserving prototype status in active names while also avoiding ceremonial test duplication.

### 4. Replace the retirement audit with a mandatory positive boundary audit

`tools/audit-protocol-v1-absence.py` and the repeated “Audit retired protocol-v1 surfaces” CI steps are removal-phase controls, not permanent product capabilities. Before deletion, each enduring assertion is assigned to a positive owner:

- Direct adapter registration and stale-option inertness: libretro adapter tests.
- Versioned wire identity and legacy rejection: codec/session/adapter tests.
- Android binary identity: Android build inspection.
- Current documentation, target names, product/session boundary, and non-reuse of retired identities: `tools/audit-gba-wifi-link-boundary.py`.
- Deleted v1 path/symbol absence: archived PR #14 evidence and Git history, not every future sanitizer run.

The replacement SHALL not be a renamed negative v1 audit. `tools/audit-gba-wifi-link-boundary.py` is mandatory and asserts the current architecture positively. It verifies that the product façade exists and is the sole libretro GBA Netpacket registration path; the stale selector key is absent; current product source, targets, diagnostics, and documentation use canonical names; `V2`/`v2` is confined to an explicit protocol/session/schema/digest/golden/compatibility/historical allow-list; no canonical target reuses the retirement inventory; the Android binary contains both `mgba-gba-link-replicated-v2` and the canonical product diagnostic identity; and protocol-v2 golden constants and schema types remain in the versioned layer.

The source/generated-target audit runs once in the fixture/tooling job. The binary portion runs once in the Android job. Ordinary normal and sanitizer tests own behavioral invariants; they do not repeatedly run the historical retirement inventory.

### 5. Replace the selection capability with the current runtime capability

The authoritative `gba-link-runtime-selection` requirements are removed after their live guarantees move to new `gba-wifi-link-runtime` requirements. The old capability directory is deleted when the delta is synced; its creation and retirement remain in archived OpenSpec.

Product-level prose in other current specs may replace “protocol-v2 adapter/runtime” with “GBA Wi-Fi Link,” “replicated link runtime,” or “paired Netpacket adapter.” Technical references to the concrete session, exact strings, and compatibility rules continue to say v2 where the version is semantically required.

### 6. Separate current instructions from historical evidence

Current product surfaces—README, operating guide, roadmap, active validation commands, qualification helpers, analyzer messages, CI and provenance instructions—use canonical names. The technical wire reference remains explicitly version 2 and may keep `gba-link-protocol-v2.md` because it documents that precise contract.

Historical evidence keeps old branch names, test names, v1/v2 comparisons, artifact paths, hashes, and conclusions where altering them would make the record inaccurate. If a historical file is easily mistaken for current instruction, it receives or retains an explicit historical status rather than being rewritten.

### 7. Remove obsolete CI and provenance branch assumptions

The protected workflow runs on pull requests and current `master`, not the three obsolete `feature/wifi-link-netplay*` push branches. `UPSTREAM.md` describes the current fork/master patch stack and the process for a future upstream refresh; old branch commands remain only where clearly identified as historical provenance.

### 8. Treat unchanged bytes and behavior as the primary gate

Before renaming, record:

- protocol name, magic, protocol/runtime versions, message/reason values and v2 golden packets;
- determinism-profile and calibration-vector digests;
- focused target/test inventory and paired replay outputs;
- adapter/session failure, input mapping, cable transaction, audio and checkpoint results.

After the rename, the same golden values and semantic results must match. The positive boundary audit verifies that product-facing definitions live behind the canonical façade, session/wire definitions remain versioned, and no canonical name reuses a retired v1 identity. No commercial physical replay is required unless review finds a runtime behavior change.

### 9. Keep structured diagnostics separate from prose

User-facing prefixes and explanatory sentences become canonical GBA Wi-Fi Link wording. Machine-readable qualification records retain stable record kinds, role and session correlation fields, units, field meanings, and privacy guarantees. If any machine-readable contract must change, it receives an explicit diagnostic schema version and the validator, analyzer, fixtures, and documentation migrate together. Qualification tools SHALL not parse changeable prose to recover structured state.

## Risks / Trade-offs

- **Large mechanical symbol churn hides an accidental semantic edit** → Split commits by boundary, use mechanical rename-only commits where possible, inspect semantic diffs separately, and compare all golden/replay evidence.
- **Overzealous cleanup changes the wire identity or digest input** → Maintain an explicit immutable allow-list and fail tests if any golden packet, magic, version, numeric enum, or SHA-256 fixture changes.
- **Removing the retirement audit loses a useful invariant** → Produce an assertion-to-owner table first and install the mandatory positive product/session/wire-boundary audit before deleting the historical removal audit.
- **Canonical names resurrect retired v1 identities** → Use the product-specific `gba-wifi-link` / `mLibretroGBAWifiLink*` namespace and mechanically reject every retired path, symbol, target, or configuration identity.
- **Renaming active spike tests discards historical traceability** → Keep old names in labelled historical reports and record old-to-new target mappings in the change evidence.
- **Current documentation still reads as a migration report** → Define a current-document allow-list and review README, operating guide, roadmap, provenance and CI as product surfaces rather than applying a global textual substitution.
- **Future incompatible protocol work lacks a naming home** → Keep `session-v2` and `protocol-v2` as the explicit compatibility layers; a future generation must be separately designed behind the same product façade.

## Migration Plan

1. Capture the exact pre-rename wire, digest, test, replay and source-name inventory.
2. Freeze the `session-v2` and `protocol-v2` paths, symbols, targets, and golden compatibility boundary.
3. Rename the libretro adapter to the product-specific GBA Wi-Fi Link façade, then rename paired replay and current user-facing diagnostics.
4. Inventory and rename/delete active spike harnesses and qualification paths according to unique coverage.
5. Add the positive GBA Wi-Fi Link boundary audit, migrate ordinary behavioral ownership, and remove the retirement-only audit/CI steps.
6. Update CMake, CI, current tools, specifications and current documentation; preserve historical evidence.
7. Run canonical-name/immutable-wire audits, focused normal and sanitizer suites, complete suite, helpers/analyzers, paired replay and Android build inspection.
8. Land through protected `master`, sync and archive the OpenSpec change, and update roadmap/release issue language.

Rollback is a normal revert of the integration commits. Because the wire contract and user configuration do not change, no peer, save, or configuration migration is required.

## Open Questions

None block implementation. The active spike-harness inventory determines whether each harness is renamed or deleted, but the decision rule and required coverage preservation are fixed above.
