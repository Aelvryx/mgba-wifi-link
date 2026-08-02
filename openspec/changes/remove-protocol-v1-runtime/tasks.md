## 1. Establish the removal boundary

- [x] 1.1 Create the implementation branch from protected `master`, record the exact base commit, and capture the currently passing focused protocol-v2, generic SIO, and paired-replay baseline.
- [x] 1.2 Inventory every non-archived v1 source, header, symbol, build target, core option, frontend branch, C/Python test, packet-log analyzer, analyzer CI invocation, helper, v1-only spike configuration, and current-documentation reference.
- [x] 1.3 Build a dependency map for every definition v2 currently obtains through `protocol.h`, `session.h`, or another v1-owned header; classify each as neutral/v2-required or retired.
- [x] 1.4 Classify every v1 test case and distinct behavioural invariant it uniquely covers as retired wire/timeline behaviour, generic SIO/local-lockstep behaviour, or lifecycle/transport behaviour still required by v2, and name the surviving owner for every retained invariant without inventorying individual assertion statements.
- [x] 1.5 Record the protocol-v2 golden packet fixtures, runtime compatibility version, paired-replay state digests, and relevant test counts that must remain unchanged.

## 2. Separate the surviving v2 substrate

- [x] 2.1 Move endpoint-role, identity-size, decode-status, copied-queue, transport-diagnostic, and other genuinely shared definitions out of v1-owned protocol/session headers into neutral or explicit v2 ownership.
- [x] 2.2 Update protocol-v2, identity, RTC, input, session-v2, transport, adapter, and test includes to use the surviving ownership without retaining a v1 packet or session dependency.
- [x] 2.3 Remove or replace v1-only reason values and constants from the surviving transport interface while preserving the current v2 failure mapping and diagnostics.
- [x] 2.4 Verify no protocol-v2 encoded byte, runtime compatibility value, selector result, or callback-generation rule changed during dependency separation, and preserve the existing numeric identities of host/client roles, SHA-1 identity length, copied-queue capacity, v2-used decode statuses, and surviving generic transport reasons.
- [x] 2.5 Build and run the protocol-v2 codec, identity, RTC, input-sync, session-v2, transport, adapter, and paired-replay tests before deleting v1 sources.

## 3. Make libretro unconditionally protocol v2

- [x] 3.1 Remove the `mgba_gba_link_netplay_runtime` core-option declaration and its `cable-v1` and `replicated-v2` selector values.
- [x] 3.2 Remove `netplayV1Diagnostic`, the stale option query, and every v1/v2 branch in frame execution, input ownership, configuration updates, reset, unload, serialization, cheats, and session-state guards.
- [x] 3.3 Register, run, present, reject protected operations through, reset, and unload the v2 adapter directly for supported GBA Netpacket use while preserving ordinary disconnected execution.
- [x] 3.4 Add frontend regressions proving the runtime selector is absent, `mgba_gba_link_netplay_runtime` is never queried or acted upon for stale values `cable-v1`, `replicated-v2`, and an arbitrary value, and Netpacket registration reaches v2.
- [x] 3.5 Confirm non-GBA and GBA-without-Netpacket load/run/reset/state/cheat/unload paths remain unchanged.

## 4. Remove the v1 implementation and targets

- [x] 4.1 Delete the distributed-SIO packet codec, host-led timeline, v1 session, `GBASIONetplayDriver`, and their v1-owned internal headers after all surviving dependencies have moved.
- [x] 4.2 Delete the libretro v1 Netpacket adapter and header and remove them from all production and test builds.
- [x] 4.3 Delete v1-only codec, session, driver, integration, and libretro-adapter tests after completing the assertion-ownership inventory.
- [x] 4.4 Remove v1-only source/test entries, the Python packet-log analyzer and self-test, their active CI invocation, and v1-only spike configurations from GBA, libretro, CMake, source-group, install/export, tool, and CI surfaces without weakening surviving v2 test or fixture discovery.
- [x] 4.5 Add a static absence audit over clean production-header/source searches, a clean CMake generated target list, active tests/tools, Android shared-object strings/symbols, and current instructions; allow only archived OpenSpec, clearly labelled historical evidence, and one bounded raw legacy-wire v2 test fixture.
- [x] 4.6 Add protocol-v2 rejection regressions using a bounded raw legacy header—without the v1 codec—proving attachment-time input produces zero replica captures/manifests and ready-state input is rejected before dispatch then performs bounded malformed-packet teardown and accepted-checkpoint restoration.

## 5. Preserve generic regression value

- [x] 5.1 Migrate each inventoried distinct generic common-SIO invariant to `gba-sio` or another transport-independent test and verify completion, IRQ, busy, line-state, and receive-word coverage remains explicit.
- [x] 5.2 Migrate each inventoried distinct local topology or scheduling invariant to lockstep/replicated-pair tests and verify pre-execution P0/P1 topology and transfer behaviour remain covered.
- [x] 5.3 Migrate each inventoried distinct generation, queue, callback re-entry, state/cheat guard, reset, unload, and teardown invariant still required by v2 to transport, session-v2, adapter, or replay tests.
- [x] 5.4 Document by test case and distinct invariant which v1-only grant, mode-barrier, transfer, completion-decision, and distributed failure behaviours were intentionally retired rather than migrated.
- [x] 5.5 Run the surviving focused suite and confirm its coverage inventory and target count match the post-removal expectation rather than the obsolete pre-removal count.

## 6. Update current documentation and tooling

- [x] 6.1 Update the root README and current Wi-Fi link guide so setup describes only protocol-v2 registration and latency-policy selection; document that stale runtime-selector lines are ignored and removable.
- [x] 6.2 Update the protocol-v2 overview, upstream/provenance notes, qualification configurations, and active validation commands to remove live v1 selection and deleted target names.
- [x] 6.3 Update the Four Swords qualification validator/helper and mocked fixtures so they require protocol-v2 runtime identity without requiring `mgba_gba_link_netplay_runtime`.
- [x] 6.4 Preserve archived OpenSpec content unchanged and retain v1 performance/packet evidence with an explicit retired-historical label and no current selection instructions.
- [x] 6.5 Update the validation matrix to separate active v2 evidence from retired v1 evidence and to list the post-removal focused commands and targets accurately.
- [ ] 6.6 Update the roadmap and GitHub issue #6 status only when the implementation and protected validation are complete; do not change the one-frame default or close hotspot issue #9.

## 7. Validate the v2-only core

- [x] 7.1 Run formatting/static checks, the scoped v1 absence audit with its narrow allow-list, and strict OpenSpec validation for `remove-protocol-v1-runtime`.
- [x] 7.2 Build and run all surviving focused tests in the normal configuration, including generic SIO, lockstep, replica, protocol-v2, session-v2, transport, adapter, and paired replay.
- [x] 7.3 Run the focused ASan/UBSan suite with leak detection and fail-fast settings.
- [x] 7.4 Run the focused TSan suite and confirm no new race or callback-lifetime report.
- [x] 7.5 Run the complete normal mGBA suite and compare only against the documented upstream baseline exception policy.
- [x] 7.6 Reproduce surviving v2 analyzers, shared diagnostic fixtures, and qualification-helper tests using the v2-only configuration; no retired v1 analyzer runs in active CI.
- [x] 7.7 Build and inspect the Android ARM64 libretro shared object and prove it contains protocol-v2 identity but no v1 compatibility string or entry point.
- [x] 7.8 Run the paired protocol-v2 replay and compare golden packets, runtime compatibility, state traces, frame/input mapping, cable transactions, audio accounting, and teardown with the pre-removal baseline.
- [x] 7.9 Confirm the diff contains no protocol-v2 wire, calibration, selector, input-delay, RTC, replica, save-ownership, scheduling, or teardown behaviour change beyond dependency ownership.

## 8. Review and land the removal

- [x] 8.1 Organize the patch so neutral/v2 dependency extraction is reviewable separately from the mechanical v1 deletion and documentation updates.
- [ ] 8.2 Obtain focused review of the removal boundary, migrated test ownership, static absence result, and claim that protocol-v2 behaviour and wire bytes are unchanged.
- [ ] 8.3 Open the implementation PR linked to GitHub issue #6 and record exact-head local validation plus the expected protected CI matrix.
- [ ] 8.4 Require all protected GitHub checks to pass on the immutable reviewed head; no physical commercial rerun is required unless review finds a production v2 behaviour change.
- [ ] 8.5 Sync the approved delta specifications, archive `remove-protocol-v1-runtime`, merge through protected `master`, close issue #6, and leave the workspace clean.
