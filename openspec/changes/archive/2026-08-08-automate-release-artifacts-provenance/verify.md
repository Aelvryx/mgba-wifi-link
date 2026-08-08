# Verification Report

**Change**: `automate-release-artifacts-provenance`  
**Verified at**: 2026-08-08 08:00 Europe/London  
**Verifier**: Codex, inline

## Summary

| Dimension | Status |
|---|---|
| Completeness | PASS — 47/47 implementation tasks; 16 requirements and 30 scenarios covered |
| Correctness | PASS — local tooling plus all six protected jobs green |
| Coherence | PASS — implementation follows the trusted-maintainer, one-tag design |

No Critical, Warning, or Suggestion finding remains.

## 1. Structural validation

`openspec validate --all --json` returned six items, all with `"valid": true`:
the active change and all five authoritative specifications. Informational
long-requirement notices in pre-existing GBA link specifications are not
failures and are unrelated to this change.

## 2. Task completion

All 47 checkboxes in `tasks.md` are complete. The post-verification
retrospective, archive, final protected run, readiness, and merge sequence is
tracked in `plan.md` because it is the schema lifecycle following this report,
not unfinished implementation.

## 3. Delta specification sync state

| Capability | State before archive | Note |
|---|---|---|
| `automated-release-provenance` | Needs sync | New capability; archive will create the authoritative specification |
| `public-alpha-distribution` | Needs sync | Archive will apply the approved automated-publication delta |

This expected pre-archive state is not a blocker.

## 4. Requirement and design coherence

| Contract area | Implementation and scenario evidence |
|---|---|
| Approved annotated tag and exact source | `admission.py`, `test_admission.py`, and the `inspect-tag`/`admit` workflow jobs validate canonical annotated tags, the peeled commit, `master` reachability, tracked notes, and the real tag-push event |
| Protected evidence and two matching Android builds | `gba-wifi-link-release.yml` reuses the six-job workflow, performs two clean pinned builds, checks embedded identities, compares bytes, and seals one handoff; `test_workflow_policy.py` covers missing/mismatched paths |
| Deterministic package and acyclic provenance | `packager.py`, `provenance.py`, `verifier.py`, and their tests cover exact membership, normalized ZIP bytes, both checksum scopes, self-contained provenance, and two-run equality |
| Privacy and bounded interfaces | `privacy.py`, `text_policy.py`, `resource_limits.py`, and `github.py` enforce public allow-lists, category-only errors, bounded duplicate-safe JSON, bounded downloads, and ZIP expansion limits |
| Automatic transactional publication | `publisher.py` and `test_publisher.py` cover draft creation, seven uploads, read-back, publication, cleanup, ambiguous responses, conflicts, and exact read-only retries without a second approval |
| Proportionate project guidance | `docs/gba-wifi-link-release.md`, README, ROADMAP, SUPPORT, issue forms, boundary audit, issue #21, and milestone #2 describe one trusted tag, fully automatic publication, neutral feedback, and no invented device replay |

The concrete implementation matches design decisions D1–D9. In particular,
only the final job has `contents: write`; there is no controller workflow,
ruleset-audit secret, manual dispatch, signed-attestation dependency, or
rehearsal repository. Volatile GitHub run/job IDs are validated internally but
excluded from public bytes, preserving reproducible exact retries.

## 5. Implementation signal

- Worktree was clean before this report was created.
- Commit range: `9e919b0cfbb93af7d1171570dfc6745d00eeebab..a6ce6f8778fba9dd47583b2f30b1886bf8218abe`.
- The range is pushed to draft PR #31.
- No file under `include/`, `src/gba/`, or the GBA Wi-Fi Link runtime execution
  path differs from `origin/master`.
- Protected run `31246907499` passed all six jobs on implementation head
  `00adc2edcbcd8ef3cf21813c8a30d709f2481b62`; the following commit changed only
  the OpenSpec task ledger.

Fresh local evidence:

- 125/125 release-tool and workflow-policy tests;
- 15/15 boundary-policy tests and the source boundary audit;
- 29/29 Android qualification-helper tests and analyzer smoke;
- two clean synthetic releases recursively and archive-byte identical;
- Python, shell, duplicate-safe JSON, YAML, strict OpenSpec, and diff checks.

## 6. Front-door routing leak detector

`docs/superpowers/specs/*.md` matched no files. No routing leak exists.

## 7. Deferred manual dogfood equivalence

`plan.md` contains no `[~]` deferred task. A disposable release repository and a
new production tag were deliberately removed from scope, not deferred. The
parser-faithful GitHub fake, transaction tests, deterministic double package,
workflow-policy tests, and protected Android/CI jobs cover the approved
automation contract. The first later maintainer-approved version tag is the
first live publication.

## Overall decision

**PASS — ready for retrospective, specification sync/archive, final protected
validation, and branch finishing.**
