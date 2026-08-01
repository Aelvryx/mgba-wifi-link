## 1. Baseline retest and branch decision

- [x] 1.1 Create a focused feature branch from the merged alpha.2 baseline and record source commit, core hash, RetroArch version, device identities, and current Four Swords status.
- [x] 1.2 Define ignored local paths for the user-owned Four Swords ROM, saves, savestates, private input scripts, screenshots, and raw logs; verify none can enter source control or release packaging.
- [x] 1.3 Prepare a physical-run manifest containing run ID, exact core hash, initial-state/configuration identities where available, isolated options, expected frontend state, success/failure signals, time box, and human action checklist.
- [x] 1.4 Automation-stage the exact alpha.2 core and synchronized logging on both devices, hand off one topology-settled Four Swords discovery retest to the human, then capture the outcome, perform safe teardown/cleanup, and preserve the same evidence required by applicable Stage 7 qualification tasks.
- [x] 1.5 If alpha.2 links, add a non-commercial regression for the already-correct topology transition, record task 1.4 as satisfying this branch's physical qualification, explicitly decide whether observer infrastructure warrants a separate change, and mark conditional tasks 2–7 not applicable without repeating the physical run.
- [x] 1.6 Not applicable — alpha.2 linked successfully, so the known-failure/Stage A branch was not activated.

## 2. Stage A structured observer, conditional on task 1.6

> Not applicable: task 1.4 proved that the topology-settled alpha.2 baseline
> links and enters shared gameplay. Permanent observer infrastructure is a
> separate future decision.

- [x] 2.1 Not applicable — baseline-success path.
- [x] 2.2 Not applicable — baseline-success path.
- [x] 2.3 Not applicable — baseline-success path.
- [x] 2.4 Not applicable — baseline-success path.
- [x] 2.5 Not applicable — baseline-success path.
- [x] 2.6 Not applicable — baseline-success path.
- [x] 2.7 Not applicable — baseline-success path.
- [x] 2.8 Not applicable — baseline-success path.
- [x] 2.9 Not applicable — baseline-success path.
- [x] 2.10 Not applicable — baseline-success path.

## 3. Stage A deterministic replay and comparison tooling

> Not applicable: the conditional Stage A observer was not introduced.

- [x] 3.1 Not applicable — baseline-success path.
- [x] 3.2 Not applicable — baseline-success path.
- [x] 3.3 Not applicable — baseline-success path.
- [x] 3.4 Not applicable — baseline-success path.
- [x] 3.5 Not applicable — baseline-success path.
- [x] 3.6 Not applicable — baseline-success path.
- [x] 3.7 Not applicable — baseline-success path.
- [x] 3.8 Not applicable — baseline-success path.
- [x] 3.9 Not applicable — baseline-success path.
- [x] 3.10 Not applicable — baseline-success path.

## 4. Stage A strict ladder and mandatory review gate

> Not applicable: task 1.4 supplied decisive successful device evidence, so
> there is no failing boundary to diagnose or convert into a Stage B delta.

- [x] 4.1 Not applicable — baseline-success path.
- [x] 4.2 Not applicable — baseline-success path.
- [x] 4.3 Not applicable — baseline-success path.
- [x] 4.4 Not applicable — baseline-success path.
- [x] 4.5 Satisfied directly by task 1.4's decisive physical evidence.
- [x] 4.6 Not applicable — no failing layer exists in this baseline run.
- [x] 4.7 Not applicable — no discovery-time NORMAL-mode failure was observed.
- [x] 4.8 Not applicable — no production delta is proposed.
- [x] 4.9 Not applicable — Stage B was not activated and production behavior did not change.
- [x] 4.10 Not applicable — packets and wire ordering were not implicated.

## 5. Stage B reviewed correction and causal regression — blocked until task 4.9

> Not applicable: no Stage B behavior change was required or authorized. The
> test-only guest-visible topology assertion is the baseline-success
> regression required by task 1.5.

- [x] 5.1 Not applicable — no production correction.
- [x] 5.2 Not applicable — no reviewed failing behavior; task 1.5 strengthens the already-correct topology regression instead.
- [x] 5.3 Not applicable — no new fixture or guest code.
- [x] 5.4 Not applicable — no production correction.
- [x] 5.5 Not applicable — no production behavior was added.
- [x] 5.6 Not applicable — no diagnosed divergence existed to replay.

## 6. Automated regression and performance gates after Stage B

> Not applicable as a Stage B matrix because production behavior did not
> change. The strengthened replicated-pair suite is still run normally, and
> the exact alpha.2 CI/Android evidence remains the production baseline.

- [x] 6.1 No Stage B matrix required; the complete 17-executable focused suite and changed 14-case replicated-pair suite pass normally.
- [x] 6.2 No broader Stage B matrix required; the changed replicated-pair suite passes under ASan/UBSan with leak detection and under TSan.
- [x] 6.3 Not applicable — no production behavior changed; alpha.2 complete-suite baseline is already green apart from the pinned upstream hash case.
- [x] 6.4 Not applicable — no observer, analyzer, adapter, or fixture behavior changed.
- [x] 6.5 Not applicable — no production behavior changed; task 1.4 supplied the physical runtime evidence.
- [x] 6.6 Not applicable — no observer exists in this branch.
- [x] 6.7 Not applicable — no observer exists in this branch.
- [x] 6.8 Not applicable — the exact alpha.2 ARM64 artifact and CI build are already recorded.

## 7. Exact-head physical qualification after Stage B — baseline-success path is satisfied by tasks 1.4–1.5

- [x] 7.1 Satisfied by task 1.4: exact alpha.2 staging, isolated configuration, and controller/overlay preflight were captured.
- [x] 7.2 Satisfied by task 1.4: navigation and gameplay remained entirely human-owned.
- [x] 7.3 Satisfied by task 1.4: both logs and terminal screenshots were captured without automation driving gameplay.
- [x] 7.4 Satisfied by task 1.4: shared gameplay reached 27,000 frames and 110,852 transfers with matched traces, normal input/audio/animation, and no protocol or SIO failure.
- [x] 7.5 Not applicable — Four Swords succeeded.
- [x] 7.6 Satisfied by task 1.4: RetroArch stopped, run-specific device directories were removed, and both displays reached asleep/dozing state.
- [x] 7.7 Not applicable — Stage B made no production behavior change.

## 8. Documentation and final review

- [x] 8.1 Update compatibility, validation, protocol/runtime, trace-schema, and provenance documentation with the no-change result, regression mapping, hashes, and physical outcome.
- [x] 8.2 Not applicable — no observer, trace schema, or comparator was introduced; privacy/content exclusions remain documented.
- [x] 8.3 Reconcile every conditional/not-applicable task with evidence and do not count a discovery-screen dwell as successful qualification.
- [x] 8.4 Run strict OpenSpec validation and require the immutable final head to pass all six CI jobs; record that post-commit run ID in the PR Checks evidence rather than creating a new bookkeeping head after it passes.
- [x] 8.5 Present the baseline proposal, Android qualification tooling, test-only topology regression, and result documentation as independently reviewable commits; diagnostic infrastructure, reviewed-delta, and Stage B commits are not applicable.
- [x] 8.6 Open draft PR #4 with the baseline-success evidence and request focused independent review before merging or publishing a replacement alpha.

## 9. Independent-review remediation

- [x] 9.1 Restrict run IDs to one normal path component, validate local and remote containment before every staging or destructive action, and refuse an existing remote run directory.
- [x] 9.2 Parse each endpoint's latest effective Android Autoconf assignments, require the expected physical AYN controller on port 1, and reject `Virtual`, displaced, absent, stale, or ambiguous assignments.
- [x] 9.3 Make the private run manifest an enforced helper input; validate effective final configuration values, exact artifact identity, frontend/content/runtime identity, isolated paths, fresh staging, and local-to-remote hashes before human handoff.
- [x] 9.4 Record the app-private installed-core hash as unavailable with an explicit reason, distinguish staged-artifact custody from loaded-core identity evidence, and document the required human-owned core installation/identity confirmation.
- [x] 9.5 Add a mocked ADB/config/log/manifest qualification-helper regression covering path escape, stale directories, ordering, identity/hash failures, and endpoint-specific controller acceptance; run it in CI.
- [x] 9.6 Make every unimplemented Stage A observer, replay, comparison, review-gate, and Stage B regression requirement explicitly conditional on activation by the baseline-failure branch.
- [x] 9.7 Run the mocked helper test, shell syntax checks, focused repository validation, and strict OpenSpec validation after remediation.
- [x] 9.8 Make this bookkeeping commit the final source head, then update only the PR evidence with its completed exact-head workflow run so the recorded CI is not invalidated by another commit.
