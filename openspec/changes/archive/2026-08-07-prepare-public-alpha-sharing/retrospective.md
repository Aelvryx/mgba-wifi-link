# Retrospective: prepare-public-alpha-sharing

> Written: 2026-08-07 (after verification passed)  
> Commit range: `b7cadcd4892c09a939805c16f3b9babbd0fe3051..d3a77ebd5760b154261078eada66f0990c29671d`  
> Worktree: isolated feature worktree (local path omitted from public evidence)

---

## 0. Evidence

- **Commit range**: `b7cadcd48..d3a77ebd5` (14 commits)
- **Diff size**: +1,727 / -13 lines across 12 files
- **Tasks done**: 32/32
- **Active hours**: approximately 5.3 hours from first planning commit to final
  review-fix commit
- **Subagent dispatches**: 16 implementer/reviewer dispatches, with scoped
  follow-up turns for two review fixes
- **New external dependencies**: none
- **Bugs encountered post-merge**: none; the change was still on a draft PR
- **OpenSpec validate state at archive**: pre-archive verification passed 5/5
  items; archive validation remained the next lifecycle step
- **Test coverage signal**: all six protected GitHub jobs passed on exact head
  `d3a77ebd5`; strict OpenSpec, boundary, privacy, issue-form YAML, deterministic
  archive, both checksum scopes, and public redownload checks passed

Commit chain (chronological):

```text
d665026c2 spec: plan public alpha sharing readiness
769a83994 spec: correct boundary audit invocation
00ee3482c chore: record verified release baseline
5698c683b docs: add reviewable v0.2.0 release inputs
db94328bc chore: record release-input completion
721f065f4 docs: record public alpha repack evidence
5056e5357 chore: record package assembly completion
154648e41 chore: record public alpha validation gate
e0ee28cfd docs: record corrected v0.2.0 assets
65c237102 docs: close public alpha release correction
35b4e590e docs: adopt proportionate alpha onboarding
c3117819c docs: hand off public alpha readiness
80b860a31 docs: prohibit raw public link logs
d3a77ebd5 docs: complete public alpha review fixes
```

---

## 1. Wins

- The release repair was genuinely transactional: the original and intermediate
  public states were snapshotted, replacement occurred while the prerelease was
  hidden, and clean redownloads proved each public state before visibility was
  restored (`docs/public-alpha-readiness.md`).
- Executable evidence stayed frozen. The source tag, Android core, and both CC0
  fixtures retained their original identities while only documentation,
  archive, checksum, and release-metadata surfaces changed.
- The deterministic package was rebuilt twice from clean staging roots with
  byte-identical output, exact member allow-lists, internal and external
  checksum scopes, licence text, and source provenance.
- The revised personal-project schema enabled the correct safety order: draft PR
  and protected checks before consequential release mutation, while keeping PR
  readiness and merge as the final lifecycle steps (PR #29).
- Layered reviews found real cross-surface defects before merge: public-log
  solicitation in the guide and issue form, and omitted fixture hashes in the
  live release body. Each was corrected without manufacturing runtime testing.
- The user-facing process was simplified when it stopped serving the objective:
  the staged cold-reader ceremony was replaced by a documented limited-alpha
  feedback policy (`35b4e590e`).

## 2. Misses

- 🟡 **Painful:** The initial plan made a staged twenty-minute cold-reader
  rehearsal a blocking gate despite existing playtesting, reviewed guidance,
  and independently verified artifacts. It created coordination overhead with
  little additional confidence and required a six-artifact planning revision.
- 🟡 **Painful:** Privacy policy was checked too narrowly at first. The packaging
  guide requested endpoint logs, and after that was fixed the ordinary issue
  form still requested log excerpts. The audit should have treated guide,
  support policy, issue forms, and release notes as one public-reporting surface.
- 📌 **Nit:** The first corrected release body omitted the two fixture hashes
  even though the artifacts and manifests carried them. Cross-surface identity
  should have been checked as a field matrix rather than by prose inspection.
- 📌 **Nit:** Task 7 was marked complete before its independent review finished;
  the SDD review loop corrected this, but task bookkeeping should follow—not
  anticipate—the review verdict.

## 3. Plan deviations

| Plan task | What changed | Why |
|---|---|---|
| Task 4 | A working draft PR opened before implementation completion | Protected CI and review were prerequisites for safe release mutation; schema v1.0.1 explicitly supports this personal-project workflow. |
| Task 6 | Cold-reader rehearsal replaced by a proportionate onboarding-feedback policy | The user correctly identified that the staged exercise had become ceremony; existing evidence was sufficient for limited alpha sharing. |
| Task 7 | Implementation handoff was separated from verify/retrospective/archive/merge | The schema owns those lifecycle steps after all apply tasks finish, preventing circular or premature task completion. |
| Task 7 review fixes | Added guide/SUPPORT/issue-form privacy alignment and complete release-body identities | Cross-surface final review found two genuine documentation-policy gaps. |
| Publication | Documentation assets were repacked a second time | The first public guide still solicited raw endpoint logs; the second repack corrected the public artifact without changing runtime bytes. |

## 4. Skill / workflow compliance

| Skill | Used |
|---|---|
| `superpowers:brainstorming` | ✓ |
| `superpowers:writing-plans` | ✓ |
| `superpowers:using-git-worktrees` | ✓ |
| `superpowers:subagent-driven-development` | ✓ |
| (transitive) `superpowers:test-driven-development` | ✓ where applicable; documentation/tooling changes used validation-first evidence |
| (transitive) `superpowers:requesting-code-review` | ✓ per task and whole branch |
| `superpowers:finishing-a-development-branch` | ⏳ pending by schema order; invoked only after retrospective and archive |

### Deliberately Skipped Skills

None. `superpowers:finishing-a-development-branch` is not skipped: this
retrospective is deliberately written before the archive and finishing steps,
so its use cannot truthfully be recorded at write-time. The schema needs a
distinct pending-by-order status for this row rather than treating it as a
skip.

## 5. Surprises

- The public repository had already been polished, but the downloadable guide
  predated that pass and still contained private qualification terminology.
- GitHub supported temporarily returning the prerelease to draft, which made the
  non-atomic asset replacement safely recoverable.
- The final public-log contradiction lived outside the release package in the
  issue form; repository-wide public-surface review was necessary even for a
  packaging-focused change.
- The retrospective template asks whether the finishing skill was used even
  though the schema requires the retrospective before that skill runs.

## 6. Promote candidates → long-term learning

- [ ] 🟡 **Audit privacy/reporting policy across every public intake surface as
  one contract** → **Promote to schema**
  > **Why**: Guide and SUPPORT corrections initially missed the issue form's log request, requiring a final review fix.
  > **How to apply**: Any release/privacy change should scan packaging guides, README/support/security text, issue forms, release notes, and automated validators together.

- [ ] 🟡 **Let project gates serve the delivery objective; do not manufacture a
  human ritual when real evidence is already stronger** → **Promote to memory**
  > **Why**: The cold-reader gate created the exact slow middle-ground testing the user had explicitly asked to avoid.
  > **How to apply**: Before adding a manual gate, state the decision it can change and waive it when reviewed artifacts, automation, and existing real use already answer that question.

- [ ] 📌 **Verify release identity as a cross-surface field matrix** → **Promote
  to schema**
  > **Why**: The first release body omitted fixture hashes even though manifests and assets were correct.
  > **How to apply**: For release work, compare source, binary, fixture, archive, guide, and manifest identities across release body, assets, manifests, bundled provenance, and readiness evidence.

- [ ] 📌 **Retrospective skill compliance needs a pending-by-order state for the
  finishing skill** → **Promote to schema**
  > **Why**: The schema requires retrospective before archive and finishing, so an all-green used/not-used table is temporally impossible at write-time.
  > **How to apply**: Allow `pending-by-order` in the retrospective template and require a later forward-pointer only if the lifecycle fails before finishing is invoked.
