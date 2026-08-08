## Context

The project already has a coherent v0.2.0 Android alpha package and protected CI.
The remaining problem is mechanical release work. This design automates that work
for a personal repository whose authorized maintainer and deliberately pushed tags
are trusted.

An earlier revision introduced a protected controller, hostile-tag defenses,
ruleset-audit credentials, signed-attestation gates, and a disposable public
rehearsal repository. Those requirements did not come from the user and are
removed here. Ordinary correctness, privacy, reproducibility, and failure safety
remain in scope.

## Goals / Non-Goals

**Goals:**

- Make one approved annotated tag the only human release action.
- Run the existing protected tests against the tagged commit.
- Produce two byte-identical Android ARM64 builds.
- Generate deterministic public assets, provenance, and checksum manifests.
- Publish automatically through a private-draft/read-back transaction.
- Make exact reruns read-only and conflicts fail closed.
- Preserve the public privacy boundary and historical v0.2.0 evidence.

**Non-Goals:**

- Defending the repository from a malicious authorized maintainer or their tag.
- A separate protected controller or workflow-to-workflow trust protocol.
- Repository ruleset governance, audit credentials, or environment secrets.
- A disposable public rehearsal repository or transparency-log exercise.
- Signed artifact attestations.
- Runtime changes, extra device qualification, stable promotion, additional
  platforms, feedback solicitation, or support commitments.

## Decisions

### D1: An authorized annotated tag is trusted

- **Choice:** A canonical annotated `vMAJOR.MINOR.PATCH` tag pushed by an
  authorized maintainer triggers the complete release workflow. The workflow
  verifies that the tag peels to the intended `master` history and that tracked
  notes/version metadata agree.
- **Reason:** The repository owner is the project trust boundary. Defending the
  project from that owner intentionally rewriting their own workflow adds no
  meaningful protection.
- **Rejected:** Treating candidate workflow code as hostile and adding a
  protected `workflow_run` controller.

### D2: One workflow owns validation through publication

- **Choice:** `.github/workflows/gba-wifi-link-release.yml` handles tag inspection,
  protected tests, two clean builds, packaging, verification, private staging,
  read-back, and automatic publication. Read-only jobs use read permissions; only
  the final publisher receives `contents: write`.
- **Reason:** This directly implements “push tag, release appears” and is easy to
  understand and maintain.
- **Rejected:** Multi-workflow event correlation, persistent governance secrets,
  and manual dispatch/publish gates.

### D3: Existing CI and two clean builds provide release evidence

- **Choice:** Invoke the existing reusable protected workflow for the peeled
  commit. Build the Android ARM64/API 21 core again in two independent clean jobs
  with the same pinned NDK/configuration, and compare bytes and SHA-256.
- **Reason:** Reproducible executable output is explicitly requested and catches
  accidental toolchain or build drift.

### D4: Packaging is deterministic and exact

- **Choice:** Generate exactly seven public project assets: core, two CC0 fixtures,
  rendered guide, deterministic archive, `RELEASE-PROVENANCE.json`, and standalone
  `SHA256SUMS`. The archive contains exactly the core, fixtures, guide,
  source/provenance notice, MPL-2.0 licence, `BUILD-PROVENANCE.json`, and internal
  `SHA256SUMS`.
- **Reason:** The v0.2.0 shape is already usable. Canonical ordering, timestamps,
  permissions, serialization, and compression make repeated packaging comparable.

### D5: Provenance is generated but not signed

- **Choice:** Canonical JSON and human-readable provenance record repository, tag
  object, peeled commit, workflow/run/job evidence, toolchain/configuration, and
  file identities. Internal and standalone SHA-256 manifests cover their exact
  non-recursive scopes. GitHub attestation envelopes are not required.
- **Reason:** This satisfies the requested provenance without creating a separate
  signing/trust product.

### D6: Privacy and resource limits fail closed

- **Choice:** Explicit file/field allow-lists reject private paths, network/device
  identities, ROM/BIOS data, saves, raw inputs, endpoint logs, commercial evidence,
  secrets, symlinks, extra files, unsafe paths, and malformed/oversized remote
  evidence. Diagnostics expose bounded categories only.
- **Reason:** Automatic public publication should not depend on a final human scan.

### D7: Publication uses a private draft transaction

- **Choice:** Verify the local release, ensure no conflicting release exists,
  create one private draft, upload exactly seven assets, download/read them back,
  verify metadata and hashes, then publish automatically. A safe pre-publication
  failure may delete only the draft created by that invocation. Public state is
  never replaced or deleted automatically.
- **Reason:** GitHub has no atomic multi-asset publication operation.

### D8: Reruns validate retained evidence at the publication boundary

- **Choice:** If the tag already has a public release, download and verify its
  retained first-run assets, manifests, provenance, body, tag, target, and
  classification before any new public mutation. Exact state is read-only
  success; drafts or conflicts fail.
- **Reason:** Retrying an exact tag must never replace a coherent publication;
  volatile workflow run/job IDs are deliberately excluded from public bytes so
  an equivalent retry remains reproducible.

### D9: Validation remains proportionate

- **Choice:** Use unit/golden tests, a parser-faithful fake GitHub boundary,
  deterministic synthetic builds, protected CI, Android build inspection, and
  source/binary boundary checks. Do not create a disposable GitHub repository or
  repeat commercial gameplay for release-tooling changes.
- **Reason:** The tests should prove the requested automation rather than rehearse
  threats outside the project model.

### D10: Historical evidence is immutable

- **Choice:** Do not rewrite `packaging/gba-wifi-link/v0.2.0`, its release, tag,
  hashes, or correction record. Future corrections use a new version/tag.
- **Reason:** Historical release evidence should remain factual.

## Risks / Trade-offs

- **Trusted tag workflow:** An authorized maintainer could deliberately alter the
  workflow in the tagged commit. This is accepted because that maintainer already
  controls the repository and releases.
- **GitHub draft transaction:** A publish response may be ambiguous. Mitigation:
  read public state back and succeed only when it is exact.
- **Toolchain reproducibility:** Two builds share the pinned platform inputs.
  Mitigation: record exact versions/hashes and compare complete core bytes.
- **Maintenance surface:** Release tooling still has code and tests. Mitigation:
  keep one workflow, one contract, and only tests for requested behavior.

## Migration Plan

1. Remove governance/ruleset/attestation/controller-only code and documentation.
2. Simplify the existing tag workflow and policy tests around the trusted-tag
   model while preserving protected gates, dual builds, package verification,
   remote read-back, and automatic publication.
3. Run the complete release-tool and protected project checks.
4. Verify no runtime source changed and no live tag/release was mutated.
5. Sync/archive the OpenSpec change and merge the existing draft PR.

Rollback is a normal revert of the tooling/workflow change. The current v0.2.0
release remains usable throughout.

## Open Questions

None. The project owner explicitly selected fully automatic trusted-tag releases
and rejected the adversarial subagent-driven expansion.
