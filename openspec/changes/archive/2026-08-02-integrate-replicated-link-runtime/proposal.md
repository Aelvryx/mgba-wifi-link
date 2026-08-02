## Why

Protocol v1 has been removed, but the surviving product façade, tests, CI, logs, and current documentation are still presented as the second branch of a comparison that no longer exists. Before the next alpha release, replicated GBA Wi-Fi Link should become the canonical product surface while its concrete version-2 session and wire contract remain explicit and byte-for-byte stable.

## What Changes

- Rename the sole libretro Netpacket adapter, permanent frontend test harnesses, active qualification files, and user-facing diagnostics to a product-specific `gba-wifi-link` / `mLibretroGBAWifiLink*` façade.
- Keep the concrete `session-v2` state machine and the `protocol-v2` codec/schema version-qualified because both implement the actual version-2 compatibility contract.
- Do not reuse any path, symbol family, test target, or configuration identity listed in the protocol-v1 retirement inventory as a canonical rename target.
- Replace the obsolete current `gba-link-runtime-selection` capability with a positive `gba-wifi-link-runtime` capability describing the sole integrated runtime; retain retirement decisions in archived OpenSpec and labelled historical evidence.
- Replace retirement-only absence-audit machinery with a mandatory lightweight positive audit of the current product façade, versioned protocol boundary, generated targets, and Android binary after migrating every other enduring invariant to ordinary source, codec, adapter, replay, or build checks.
- Rename active `spike` harness/configuration surfaces according to the permanent behavior they verify, while preserving explicitly historical feasibility and spike reports as history.
- Refresh current README, operating guide, roadmap, provenance, CI branch filters, validation commands, analyzer/helper expectations, and code-navigation links so they describe one product rather than a v2-only branch.
- Preserve ordinary disconnected GBA execution and ensure the façade never owns GB/GBC or another non-GBA runtime.
- Preserve the machine-readable qualification record contract independently from changeable human-facing prose, or introduce an explicit diagnostic schema version when that contract changes.
- **BREAKING (internal/build only):** product-façade C symbols, filenames, test target names, and active tool/configuration paths change. Versioned session/codec APIs, user configuration, on-wire packets, runtime compatibility values, saved data, and supported gameplay behavior do not change.

## Capabilities

### New Capabilities

- `gba-wifi-link-runtime`: Defines the canonical product façade, the preserved versioned session/wire boundary, optional ownership of ordinary emulation, structured diagnostics, permanent positive validation, and current-versus-historical documentation rules.

### Modified Capabilities

- `gba-link-runtime-selection`: Removes the obsolete selection/retirement capability after its current-state guarantees move to `gba-wifi-link-runtime` and its historical decisions remain archived.
- `gba-link-determinism-profile`: Uses canonical runtime terminology for active attachment and lifecycle behavior while retaining every exact versioned digest domain, schema, and compatibility rule.
- `gba-link-fixed-delay`: Uses canonical runtime terminology for calibration, session, diagnostics, and qualification behavior while retaining every exact versioned wire and selector contract.
- `gba-multipak-cable-discovery`: Refers to the canonical paired Netpacket adapter in the diagnostic ladder without changing any isolation, trace, or qualification behavior.

## Impact

- Production/internal code: the `src/platform/libretro` product façade and its CMake ownership; `src/gba/sio/netplay/session-v2.[ch]` and the versioned codec remain version-qualified.
- Tests and CI: focused target names, paired-adapter replay, permanent scheduler/integration harnesses, qualification helpers, log analyzer fixtures, workflow filters, and binary identity checks.
- Specifications and documentation: current OpenSpec capabilities, README, Wi-Fi Link guide, roadmap, provenance, validation matrix, technical protocol documentation links, and historical-document placement/labels.
- Compatibility: protocol name `mgba-gba-link-replicated-v2`, magic, version numbers, message/reason values, encoded bytes, digest inputs, golden fixtures, and legacy rejection semantics remain unchanged.
