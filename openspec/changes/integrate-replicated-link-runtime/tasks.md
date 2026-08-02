## 1. Freeze the integration boundary and baseline

- [x] 1.1 Create an implementation branch from protected `master`, record the exact base, and confirm no other OpenSpec change or dirty worktree overlaps the naming migration.
- [x] 1.2 Record the exact protocol name, magic, protocol/runtime compatibility versions, header layout, message/reason/capability/policy numeric values, deterministic-profile digests, calibration-vector digest, and protocol-v2 golden packet bytes that SHALL remain unchanged.
- [x] 1.3 Inventory every active `V2`, `v2`, `protocol-v2`, `session-v2`, `netpacket-v2`, `v2-only`, `spike`, runtime-selection, retired-v1-audit, and obsolete feature-branch reference outside archived/historical/third-party/ROM-database material.
- [x] 1.4 Classify each inventory entry into canonical product façade, versioned session/wire/schema, or historical evidence and record the intended keep, rename, remove, or relabel disposition.
- [x] 1.5 Record the focused targets and case counts, paired-adapter replay outputs, selected delays, input mapping, state traces, cable counters, audio accounting, checkpoints, and teardown outcomes used for before/after comparison.
- [x] 1.6 Inventory every distinct invariant in the active replicated-pair spike harnesses and the protocol-v1 absence audit, naming a permanent positive owner or an explicit retirement disposition for each.

## 2. Preserve the versioned session and wire boundary

- [x] 2.1 Record every path, symbol family, test target, and configuration identity in the protocol-v1 retirement inventory that SHALL remain unavailable to canonical rename targets.
- [x] 2.2 Retain `session-v2.[ch]`, `GBALinkV2Session*`, `GBALinkV2SessionState`, `GBALinkV2Deadline*`, and `GBA_LINK_V2_SESSION_*` as the explicit version-2 state-machine boundary without changing session logic.
- [x] 2.3 Retain `protocol-v2.[ch]`, `GBALinkV2Packet` and payload types, wire enums/reasons/policies, `GBA_LINK_V2_PROTOCOL_*`, profile/digest schema types, and their exact definitions at the versioned codec boundary.
- [x] 2.4 Verify identity, RTC, input-sync, transport, replicated-runtime, and façade consumers continue to use the versioned session and packet APIs without an unversioned compatibility alias.
- [x] 2.5 Retain the versioned session test source/target identity, including `test-gba-netplay-session-v2`, and verify its cases and expected failure reasons remain unchanged.
- [x] 2.6 Run codec, identity, RTC, input-sync, transport, versioned-session, replica, and replicated-pair tests after freezing this boundary.

## 3. Canonicalize the libretro adapter and diagnostics

- [x] 3.1 Rename `netpacket-v2.[ch]` to product-specific `gba-wifi-link.[ch]` and keep it as the only libretro GBA Netpacket registration path.
- [x] 3.2 Rename `mLibretroNetpacketV2*`, test-only adapter symbols, metrics types, compile definitions, and libretro call sites to `mLibretroGBAWifiLink*` ownership without introducing retired `mLibretroNetpacket*` aliases.
- [x] 3.3 Update load, run, input, presentation, audio, variable-update, reset, serialize, cheat, unload, and failure paths without changing their control flow or generation-safety behavior.
- [x] 3.4 Replace user-facing “protocol-v2 session” and registration wording with canonical GBA Wi-Fi Link diagnostics while retaining exact versioned protocol identity in Netpacket registration and technical fields.
- [x] 3.5 Rename adapter and paired-replay test sources/targets to `test-libretro-gba-wifi-link` and `test-libretro-gba-wifi-link-replay` or equally product-specific non-retired names, then update CMake/CI discovery.
- [x] 3.6 Preserve machine-readable diagnostic record kinds, schema, role/session correlation, units, and privacy while updating human-facing wording; version the diagnostic schema and migrate every producer/consumer if compatibility cannot be retained.
- [x] 3.7 Prove stale `mgba_gba_link_netplay_runtime` values remain undeclared, unqueried, and behaviorally inert after all adapter renames.
- [x] 3.8 Add or retain façade regressions proving ordinary GBA execution remains unchanged without Netpacket, without an admitted peer, and after bounded teardown.
- [x] 3.9 Prove GB/GBC and other non-GBA paths neither register nor yield execution ownership to the GBA Wi-Fi Link façade.

## 4. Graduate or retire prototype-named assets

- [x] 4.1 Map each test case and distinct invariant in `gba-replicated-pair-spike` to permanent scheduler, lockstep, replicated-pair, or threading ownership.
- [x] 4.2 Rename that harness and target for its permanent scheduler/threading purpose if it remains unique, or delete it only after every invariant is demonstrably covered elsewhere.
- [x] 4.3 Map each test case and distinct invariant in the libretro replicated-pair spike harness to permanent adapter, local-pair, presentation, or lifecycle ownership.
- [x] 4.4 Rename that harness and target for its permanent frontend-integration purpose if it remains unique, or delete it only after every invariant is demonstrably covered elsewhere.
- [x] 4.5 Rename active `tools/netpacket-spike` qualification paths and Android host/client configuration filenames to current-purpose qualification names, updating every executable instruction and helper reference.
- [x] 4.6 Preserve original spike target/path names only in explicitly historical feasibility and scheduler reports, with old-to-new mappings where needed for traceability.

## 5. Replace removal scaffolding with positive ownership

- [x] 5.1 Migrate direct adapter registration and stale-option inertness assertions from the protocol-v1 absence audit to canonical libretro adapter tests.
- [x] 5.2 Retain versioned wire identity and legacy-byte rejection in codec/session/adapter tests, including attachment-time zero-capture and ready-state checkpoint-restoration cases.
- [x] 5.3 Retain Android wire identity inspection in the Android build job and require the canonical GBA Wi-Fi Link diagnostic/symbol identity there.
- [x] 5.4 Add mandatory `tools/audit-gba-wifi-link-boundary.py` source/generated-target checks for the sole product façade, inert selector, canonical current names, explicit versioned-name allow-list, versioned session/codec ownership, and non-reuse of every retired v1 identity.
- [x] 5.5 Add the audit's Android mode proving both `mgba-gba-link-replicated-v2` and the canonical product diagnostic identity are present while versioned golden/schema ownership remains intact.
- [x] 5.6 Delete `tools/audit-protocol-v1-absence.py` and its repeated focused/Android CI invocations only after the positive boundary audit and every ordinary behavioral owner are active.
- [x] 5.7 Run the source/generated-target audit once in the fixture/tooling job and its binary mode once in the Android job rather than duplicating historical removal checks across sanitizer jobs.
- [x] 5.8 Replace obsolete workflow push branches with protected `master` while retaining pull-request execution, concurrency control, and the complete protected matrix.

## 6. Integrate current specifications and documentation

- [ ] 6.1 Add the authoritative `gba-wifi-link-runtime` capability and migrate continuing façade, optional disconnected-execution, versioned session/wire-boundary, structured-diagnostic, validation-ownership, behavior-neutrality, and current-versus-historical requirements into it.
- [ ] 6.2 Remove the obsolete `gba-link-runtime-selection` capability after its delta is synced, leaving its design/removal history under archived OpenSpec.
- [ ] 6.3 Sync canonical active terminology into determinism, fixed-delay, and Multi-Pak discovery specifications without changing exact versioned digest, compatibility, or wire language.
- [x] 6.4 Update README navigation, implementation paths, roadmap priorities/decision log, Wi-Fi Link setup/troubleshooting, and release language to describe the canonical runtime directly.
- [x] 6.5 Update `UPSTREAM.md` to describe the current fork/master patch stack and future refresh process rather than obsolete `feature/wifi-link-netplay*` branches.
- [x] 6.6 Keep the technical protocol reference explicitly version 2 and verify every exact protocol string, magic, value, layout, and digest-domain statement remains correct.
- [x] 6.7 Update active validation commands and target lists to canonical names while retaining historical branch, target, artifact, hash, and v1/v2 comparison facts in labelled evidence.
- [x] 6.8 Search current user/developer/CI/tooling surfaces for obsolete branch-comparison language and confirm every remaining `v1`, `v2`, or `spike` occurrence is wire-semantic, schema-semantic, stale-config compatibility, or clearly historical.

## 7. Prove unchanged bytes and behavior

- [x] 7.1 Run formatting and whitespace checks plus `tools/audit-gba-wifi-link-boundary.py` over active source, headers, generated targets, tools, CI, current specs, instructions, Android strings, and symbols.
- [x] 7.2 Run protocol-v2 codec golden/mutation tests and prove protocol name, magic, versions, enum values, payload bytes, profile digests, and calibration digest are byte-for-byte identical to the baseline.
- [x] 7.3 Build and run every surviving focused test in the normal configuration under its canonical target name.
- [x] 7.4 Run the focused ASan/UBSan suite with leak detection and fail-fast settings.
- [x] 7.5 Run the focused TSan suite and confirm no new race or callback-lifetime report.
- [x] 7.6 Run the complete normal mGBA suite and compare only against the documented pinned upstream baseline exception policy.
- [x] 7.7 Run qualification-helper, analyzer, fixture-reproducibility, and any retained canonical consistency tests using only current-purpose active paths.
- [x] 7.8 Run the paired canonical adapter replay and compare selected delays, packet counts, input mapping, state traces, cable transactions, audio, checkpoint, failure, and teardown outcomes with the pre-rename baseline.
- [x] 7.9 Build and inspect the Android ARM64 libretro shared object, proving the exact version-2 Netpacket identity remains and canonical current diagnostics replace obsolete product-facing v2 wording.
- [x] 7.10 Review the production diff for any calibration, selector, RTC, input, replica, save, scheduling, presentation, audio, lifecycle, or teardown semantic change; require a separate specification review if one exists.
- [x] 7.11 Skip commercial physical replay when all changes are naming/ownership-only and automated evidence is identical; perform a focused exact-head smoke only if implementation review finds a production behavior change.
- [x] 7.12 Run ordinary disconnected GBA execution and non-GBA ownership regressions through the canonical façade.

## 8. Review and land the integration

- [x] 8.1 Organize the patch so versioned-session boundary preservation, product-façade rename, prototype/audit cleanup, and specification/documentation integration are independently reviewable from any unavoidable semantic edit.
- [ ] 8.2 Obtain focused review of the canonical-versus-versioned boundary, spike/audit invariant ownership, current/historical documentation split, and unchanged-wire/behavior claim.
- [x] 8.3 Open the implementation PR with exact baseline/final target mappings and protected validation evidence.
- [ ] 8.4 Require every protected GitHub check to pass on the immutable reviewed head and confirm no physical rerun was skipped after an actual behavior change.
- [ ] 8.5 Sync approved capability deltas, remove the empty `gba-link-runtime-selection` main-spec directory, archive `integrate-replicated-link-runtime`, merge through protected `master`, update the roadmap/release issue, and leave the workspace clean.
