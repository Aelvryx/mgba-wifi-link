# Trusted-tag Android release automation plan

> **Executor:** Use `superpowers:executing-plans` inline. Do not dispatch
> implementation or review agents. Use direct TDD for behavioral changes and
> `verification-before-completion` before completion claims.

**Goal:** An authorized maintainer pushes one approved annotated version tag and
the complete reproducible Android prerelease is validated, packaged, and
published automatically.

**Architecture:** One trusted tag workflow reuses protected CI, performs two
matching clean Android builds, packages deterministic provenance/checksum assets,
and publishes through a verified private-draft transaction. Repository maintainers
and their deliberately pushed tags are trusted.

**Tech stack:** GitHub Actions, Python standard library, Git/gh CLI, CMake/Ninja,
Android NDK, existing mGBA test and boundary tooling.

---

## Task 1: Remove the invented trust-controller scope

- [x] Delete `.github/workflows/gba-wifi-link-release-governance.yml` and
  `.github/rulesets/gba-wifi-link-release-tags.json`.
- [x] Delete `tag_policy.py`, its tests, and ruleset fixtures.
- [x] Remove controller/intake, ruleset credential/environment, disposable public
  rehearsal, and hostile-tag policy from release documentation and tooling.
- [x] Confirm no production tag, release, environment, secret, or ruleset is
  mutated.
- [x] Commit the scope correction as one reviewable deletion-heavy unit.

## Task 2: Simplify provenance to the requested boundary

- [x] Add failing tests showing build/release provenance records the trusted tag
  event, exact source, first-run gates/builds, toolchain, configuration, and assets
  without controller or signed-attestation fields.
- [x] Remove controller/signer/attestation fields from the contract, model,
  canonical JSON, body/source rendering, privacy allow-list, and retained parser.
- [x] Regenerate golden fixtures deliberately and prove package inventories,
  checksum scopes, and deterministic bytes remain exact.
- [x] Run admission, contract, packager, provenance, render, privacy, resource, and
  verifier suites.
- [x] Commit the provenance simplification.

## Task 3: Simplify the GitHub publication boundary

- [x] Add failing tests for publication and exact rerun without any attestation
  API call or signed evidence.
- [x] Remove attestation types/commands/verification from the GitHub client,
  publisher, existing-release verifier, CLI, fake, and tests.
- [x] Preserve supported command-specific `gh api`/`gh release` syntax, bounded
  duplicate-safe JSON, streaming no-follow downloads, exact remote read-back,
  private-draft cleanup, ambiguous-response handling, and no public replacement.
- [x] Run focused publisher/rerun tests and the full release-tool suite.
- [x] Commit the publication simplification.

## Task 4: Reduce the workflow to one trusted tag path

- [x] First make workflow-policy tests express only the approved contract:
  canonical tag trigger; no dispatch/manual approval; exact tagged source;
  protected six-job validation; two independent matching Android builds;
  deterministic double package; sealed handoff; final automatic publisher.
- [x] Simplify `.github/workflows/gba-wifi-link-release.yml` accordingly.
- [x] Keep default/read-only permissions on non-publisher jobs and only
  `contents: write` on the publisher. Remove `id-token`, `attestations`,
  controller correlation, governance environment, and ruleset API calls.
- [x] Run mutation tests for missing gates, mismatched source/build/package,
  corrupted handoff, moved tag, conflicting release, and manual-gate introduction.
- [x] Run workflow-policy and full release-tool suites.
- [x] Commit the trusted-tag workflow.

## Task 5: Align current documentation and project tracking

- [x] Update `docs/gba-wifi-link-release.md` to the complete maintainer action:
  prepare exact notes, create an annotated tag on intended `master`, push it.
- [x] Document ordinary failures, exact read-only reruns, and correction by new
  version/tag; do not describe hostile tags, controllers, audit secrets,
  disposable repositories, or attestations.
- [x] Keep README/ROADMAP/SUPPORT/issue forms neutral toward feedback and avoid a
  support promise.
- [x] Update boundary-policy expectations and negative documentation tests.
- [x] After the implementation is locally green, update issue #21/milestone once.
- [x] Commit documentation and tracking changes.

## Task 6: Run complete local verification

- [x] Run:

  ```bash
  python3 -m unittest discover -s tools/gba_wifi_link_release/tests -p 'test_*.py' -v
  python3 tools/test-audit-gba-wifi-link-boundary.py
  python3 tools/audit-gba-wifi-link-boundary.py
  openspec validate automate-release-artifacts-provenance --strict
  git diff --check
  ```

- [x] Build the synthetic release twice in distinct clean temporary directories,
  compare recursively and byte-compare the archive, then verify both sets.
- [x] Run Python compile checks, shell syntax checks, and JSON/YAML duplicate-safe
  parsing for changed release surfaces.
- [x] Compare `origin/master...HEAD` and prove no runtime source under `include/`,
  `src/gba/`, or product execution paths changed.
- [x] Record exact local evidence in the task ledger.

## Task 7: Run protected validation on PR #31

- [x] Push the simplified commits to the existing draft PR #31.
- [x] Require all six protected checks on the exact head: focused normal,
  ASan/UBSan, TSan, complete suite, fixture/tooling, and Android ARM64 build.
- [x] If a check fails, inspect its exact log and fix only a demonstrated defect;
  do not add requirements outside this specification.
- [x] Perform one inline diff/self-review covering tag admission, dual builds,
  deterministic package, privacy, transaction/rerun, workflow permissions, and
  no-runtime-change proof.

## Task 8: Close the OpenSpec cycle and merge

- [ ] Run `openspec-verify-change` and write `verify.md` from fresh evidence.
- [ ] Write `retrospective.md` while context is hot, explicitly recording the
  discarded hostile-tag/subagent expansion and the final proportional scope.
- [ ] Sync the delta specs, archive the change, and run strict validation again.
- [ ] Push final artifact/archive commits to PR #31 and require the exact final
  head checks.
- [ ] Mark PR #31 ready and squash-merge through protected `master`.
- [ ] Remove the worktree/branch only after merge and confirm v0.2.0 is unchanged.

## Deferred dogfood equivalence

No disposable public repository or production tag is created for rehearsal.
Equivalent automated evidence is provided by the parser-faithful GitHub fake,
publisher transaction/rerun suites, two clean synthetic packages, the protected
workflow-policy tests, and the exact Android/protected CI jobs. The first future
maintainer-approved tag is the first live use.
