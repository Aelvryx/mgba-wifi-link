# Retrospective: automate-release-artifacts-provenance

> Written: 2026-08-08 (after verification passed)  
> Commit range: `9e919b0cfbb93af7d1171570dfc6745d00eeebab..a6ce6f8778fba9dd47583b2f30b1886bf8218abe`  
> Worktree: `/var/home/anthony/repos/mgba-fork/.worktrees/automate-release-artifacts-provenance`

## 0. Evidence

- **Commit range**: 60 branch commits before final artifact/archive commits.
- **Diff size**: 9,564 additions and 36 deletions across 67 files before archive.
- **Tasks done**: 47/47.
- **Active time**: more than 24 hours elapsed; a substantial fraction was spent
  on requirements later removed from scope.
- **Opt-in delegation/review agents**: none. The earlier schema loop dispatched
  many implementation/review turns without an explicit user request; inline
  execution replaced it after the user intervened.
- **New external dependencies**: none. The implementation uses the existing
  Python standard library, `gh`, pinned GitHub actions, CMake/Ninja, and Android
  NDK inputs.
- **Bugs encountered post-merge**: none; the change is not yet merged.
- **OpenSpec validation**: all six items valid before archive.
- **Test signal**: 125 release-tool tests, 15 boundary tests, 29 qualification-
  helper tests, analyzer smoke, two byte-identical synthetic packages, and six
  exact-head protected jobs green.

Representative commit chain:

```text
7bce011bb plan automated release provenance
4ca1171ad freeze package contract
667e7e6d6 build deterministic Android bundles
dfb8efd36 add transactional GitHub publisher
23db534ca compose automatic release workflow
e2fa38828 rescope to the trusted-maintainer requirement
f2ac032ea remove invented governance layer
9b27ecd79 keep provenance self-contained
3d56e2ac9 remove attestation publication dependency
b300db7ca simplify trusted-tag workflow and delete dead rerun machinery
da7a97dfb keep admission focused on tagged source
a6ce6f877 record exact-head validation
```

## 1. Wins

- The final behaviour is literal and usable: one approved annotated tag runs
  validation, two builds, packaging, verification, and publication without a
  second human action (`gba-wifi-link-release.yml`, issue #21).
- Deterministic packaging and self-contained provenance are real rather than
  aspirational: two clean synthetic package trees and their ZIP bytes match,
  and volatile workflow run/job IDs are excluded from public bytes
  (`test_public_release_is_stable_across_workflow_run_and_job_ids`).
- Publication is automatic but still operationally sensible: the final job is
  the only writer, stages a private draft, verifies all seven assets, handles an
  uncertain response by read-back, and never replaces a public release
  (`publisher.py`, 21 publisher tests).
- TDD exposed concrete defects that mattered in the real requested path,
  including unsupported `gh api --repo` arguments and a missing
  `resource_limits.py` module in the canonical handoff.
- The final inline pass removed 958 lines across the principal simplification
  commits and corrected the actual event identity from legacy `create` to
  `push`.
- The emulator is untouched; all six ordinary protected jobs passed on the
  implementation head, so no device playtest was manufactured for tooling work.

## 2. Misses

- 🔴 **Blocking:** The initial design invented a requirement to remain secure
  against malicious workflow code in a tag deliberately authored and pushed by
  the trusted maintainer. That led to a controller workflow, ruleset audit,
  privileged credential, attestation service, and rehearsal design that were
  neither requested nor proportionate. The pre-mutation review then correctly
  found that the resulting system could not safely or practically run.
- 🟡 **Painful:** The subagent implementation/review loop amplified every local
  concern into another cross-agent contract and fix round. It extended a
  personal-project automation task beyond 24 hours and made review serve itself
  instead of the product. PR #32 changed `superpowers-bridge` to inline execution
  by default.
- 🟡 **Painful:** The rerun controller path accumulated a 272-line retained-
  release parser plus 327 lines of dedicated tests/resource parsing. Once public
  provenance stopped embedding volatile run/job IDs, the ordinary publisher
  handled retries and that whole subsystem was deleted in `b300db7ca`.
- 🟡 **Painful:** Even after correction, the release subsystem is larger than a
  toy fork would ideally need. Much of that size is exact packaging/privacy test
  coverage; future work should resist extending it unless a real release failure
  demonstrates the need.
- 📌 **Nit:** Issue #21, its milestone title, and the PR description retained old
  wording after the implementation changed. They were corrected once, after the
  simplified code was green.

## 3. Plan deviations

| Plan area | What changed | Why |
|---|---|---|
| Threat model | Replaced hostile-tag/controller design with a trusted authorized maintainer tag | The user required full automation, not defense against self-authored malicious workflow code |
| Attestations | Removed GitHub attestation issuance/verification and its permissions | Generated provenance and checksum manifests satisfy the requested reproducibility/provenance outcome without an external service |
| Governance | Deleted ruleset workflow, secret/environment, tracked ruleset, and disposable rehearsal | These were invented preconditions and the private rehearsal could not exercise public attestations for a personal account |
| Reruns | Removed pre-build retained-state controller; made public bytes stable and reused the normal publisher | Simpler, reproducible, and no separate orchestration path |
| Execution model | Switched from subagent-driven task/review rounds to primary-agent inline execution | The loop was the principal source of scope amplification |
| Validation | Kept the existing six protected jobs and synthetic release tests; skipped new production tag/device replay | The change touches release tooling only and must not publish while implementing its own publisher |

## 4. Skill and workflow compliance

| Skill | Used |
|---|---|
| `superpowers:brainstorming` | ✓ — captured in `brainstorm.md`, then corrected with the user |
| `superpowers:writing-plans` | ✓ — final inline plan in `plan.md` |
| `superpowers:using-git-worktrees` | ✓ — isolated change worktree |
| `superpowers:executing-plans` | ✓ — inline after the schema correction |
| `superpowers:test-driven-development` | ✓ — RED/GREEN tests for behavioural changes and simplifications |
| `superpowers:verification-before-completion` | ✓ — fresh local and protected evidence before this report |
| `superpowers:finishing-a-development-branch` | Scheduled next — schema order places it after retrospective and archive |

### Deliberately Skipped Skills

None. `finishing-a-development-branch` is not skipped; it is the next schema
step and cannot honestly run before this retrospective and archive exist.

## 5. Surprises

- “Fully automated” was briefly treated as permission to add a broad adversarial
  threat model. It meant exactly what it said: after the maintainer pushes the
  approved tag, the rest happens automatically.
- The theoretically stronger design was practically weaker: it depended on a
  high-scope secret that tag-sourced workflow code could reference, a private
  attestation entitlement unavailable to the personal repository, and a `gh`
  command form the real CLI rejects.
- Rerun reproducibility became simpler when provenance recorded durable build
  facts rather than ephemeral workflow IDs.
- Inline execution found product-path bugs quickly because one context owned
  implementation, tests, workflow, and user intent together.

## 6. Promote candidates to long-term learning

- [x] 🔴 **Do not invent an adversarial trust model for a trusted personal-project action.** → **Promoted to schema and change design**
  > **Why**: The hostile-tag assumption created most of the delay and machinery without any user requirement.
  > **How to apply**: When a finding would expand the threat model, stop and ask for explicit scope approval before implementing it.

- [x] 🟡 **Use inline execution by default; delegation and review must be explicit and bounded.** → **Promoted to `superpowers-bridge` schema v2 in PR #32**
  > **Why**: Repeated implementation/review subagents amplified local concerns into a self-sustaining loop.
  > **How to apply**: Execute plans in the primary context unless the user explicitly requests delegation or the approved plan names one independent high-risk review.

- [x] 🟡 **Treat “fully automated” literally at the human-action boundary.** → **Promoted to issue #21 and `automated-release-provenance` spec**
  > **Why**: The actual product outcome is one deliberate tag push followed by zero manual release actions.
  > **How to apply**: For every release design, enumerate user actions; after the approved tag, the count must be zero.

- [ ] 📌 **Prefer durable provenance facts over CI-attempt identifiers.** → **Promote to future release-maintenance guidance if it recurs**
  > **Why**: Removing run/job IDs made public artifacts reproducible and deleted a separate retained-release subsystem.
  > **How to apply**: When adding provenance fields, ask whether the value describes the artifact or merely the invocation that happened to build it.
