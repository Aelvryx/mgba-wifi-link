## Context

GBA Wi-Fi Link v0.2.0 is a coherent public Android ARM64 prerelease, but its
release set was assembled and later corrected by hand. The immutable runtime
payloads were preserved successfully; the expensive part was repeatedly proving
that the guide, provenance notice, archive membership, internal and external
checksum scopes, release body, and six downloadable assets all described the
same source and binaries.

The repository already has six protected CI jobs, a pinned Android NDK, a pinned
GNU Arm fixture toolchain, binary-boundary inspection, structured qualification
tools, tracked offline documentation, and a protected `master` branch. The new
release path should compose those controls rather than invent a parallel build.

The maintainer has selected full automation. Creating and pushing an approved
annotated release tag is the publication decision. Automation must not pause for
a second approval or Publish click. The project remains neutral toward feedback;
this change does not depend on a public diagnostic intake workflow.

## Goals / Non-Goals

**Goals:**

- Turn one new approved annotated semantic-version tag into a complete published
  Android ARM64 prerelease without manual packaging or upload steps.
- Fail before public visibility when source, validation, build, reproducibility,
  privacy, packaging, provenance, or remote asset verification is uncertain.
- Produce byte-identical executable and archive payloads from identical tagged
  inputs, with a separate verifiable boundary for GitHub-signed attestations.
- Keep build jobs unprivileged and confine release/attestation permissions to one
  final job that consumes already-verified artifacts.
- Make retries idempotent without permitting tag movement, asset replacement, or
  mutation of an existing conflicting release.
- Preserve the v0.2.0 historical record and existing proportionate runtime
  qualification rules.

**Non-Goals:**

- Stable-release promotion, nightly builds, automatic version selection, release
  trains, or additional platforms.
- Runtime, protocol, emulation, persistence, input, audio, or presentation work.
- A diagnostic bundle, automatic telemetry, feedback solicitation, or support
  commitment.
- Commercial content, private qualification evidence, ROM/BIOS/save data, raw
  endpoint logs, paths, addresses, or input history in release artifacts.
- A new signing service. GitHub artifact attestations may sign provenance outside
  the reproducible archive, but package bytes do not embed environment-specific
  signatures.

## Decisions

### D1: An annotated tag is the release command

- **Choice:** Trigger on a new annotated tag matching the canonical semantic
  version form `vMAJOR.MINOR.PATCH` (with a separately bounded prerelease suffix
  only when the release policy defines one). The peeled commit must be reachable
  from protected `master`, carry successful exact-commit protected checks, agree
  with all tracked version inputs, and have no pre-existing conflicting tag or
  release. `v0.x` publishes as a GitHub prerelease.
- **Reason:** A deliberate tag is an explicit maintainer action without adding a
  second manual publication gate.
- **Alternatives considered:** Publishing on a version-file merge makes ordinary
  merges dangerous. A second manual dispatch or Publish click contradicts the
  selected full-automation boundary.

### D2: The protected suite is reusable release evidence

- **Choice:** Refactor the protected workflow only as needed so the tag workflow
  runs the same normal, ASan/UBSan, TSan, complete-suite, fixture/tooling, and
  Android/binary-boundary gates against the peeled tag commit. The release
  provenance records the workflow/run/job identities and conclusions. No release
  job may substitute an older branch run merely because it tested the same tree.
- **Reason:** Release evidence must be attached unambiguously to the tagged source,
  while test definitions should have one owner.
- **Alternatives considered:** Polling arbitrary prior check runs creates a race
  and trust-selection problem. Duplicating the entire CI definition creates drift.

### D3: Two clean builds prove executable reproducibility

- **Choice:** Produce the Android ARM64 core twice in independent clean jobs using
  the same pinned Ubuntu runner image, NDK version, CMake/Ninja configuration, and
  dependency policy. Compare SHA-256 and bytes before either output is admitted.
  Verify AArch64 ELF identity, embedded full commit and product version, canonical
  product string, and versioned wire-compatibility string. One admitted core then
  feeds deterministic packaging.
- **Reason:** A second build is a direct, understandable guard against accidental
  nondeterminism or host contamination.
- **Alternatives considered:** Trusting one build proves identity but not
  reproducibility. Comparing only package manifests can conceal differing binaries.

### D4: A project-owned packager defines the canonical release set

- **Choice:** Add one tested, non-publishing release tool with distinct `build`,
  `verify`, and metadata/rendering responsibilities. It reads tracked current
  templates and per-release notes, derives tag/source identities, uses
  `SOURCE_DATE_EPOCH` from the annotated tag's target commit, and normalizes file
  ordering, timestamps, UID/GID, permissions, line endings, JSON serialization,
  and archive compression.

  The downloadable release set has seven project assets:

  1. Android ARM64 libretro core;
  2. `gba-link-test.gba`;
  3. `gba-link-continuous.gba`;
  4. rendered install-and-usage guide;
  5. deterministic Android ARM64 archive;
  6. canonical `RELEASE-PROVENANCE.json` covering the source/build identity and
     hashes of the five payload assets above; and
  7. standalone `SHA256SUMS` covering the other six project assets.

  The archive contains the core, both fixtures, guide, rendered source/provenance
  notice, MPL-2.0 `LICENSE`, canonical `BUILD-PROVENANCE.json`, and an internal
  `SHA256SUMS` covering every archive member except itself. Build provenance covers
  source, toolchain, evidence, and sibling payload identities but does not attempt
  to hash the enclosing archive. Release provenance is generated only after the
  archive and hashes the five non-provenance payload assets. The standalone checksum
  manifest is generated last and hashes all six preceding assets. This directed
  order prevents self-referential hashes. GitHub-generated source archives and
  external attestation envelopes are explicitly outside both checksum scopes.
- **Reason:** One implementation must own names, membership, identity rendering,
  checksum scopes, and reproducibility rather than spreading shell conventions
  across workflow steps.
- **Alternatives considered:** Reusing version-specific v0.2.0 files as mutable
  templates would blur historical evidence. Handwritten shell assembly is harder
  to validate transactionally and test portably.

### D5: Provenance is canonical data plus external attestation

- **Choice:** The build and release provenance JSON documents use versioned schemas
  and record repository,
  annotated tag object and peeled commit, workflow/run identities, runner image,
  pinned action/toolchain identities, build configuration, protected job results,
  deterministic epoch, and names/sizes/SHA-256 values for all project assets. A
  rendered human-readable notice and release body derive from the same model.
  GitHub build-provenance attestations cover at least the admitted core and archive
  without changing their bytes.
- **Reason:** Humans, tools, and GitHub verification should share one source of
  truth, while signed environment-specific data stays outside the reproducible
  payload.
- **Alternatives considered:** Prose-only provenance is difficult to validate.
  Embedding signed attestations in the archive destroys byte reproducibility.

### D6: Privacy validation is fail-closed

- **Choice:** The packager accepts only an explicit field and file allow-list and
  rejects absolute paths, network/device addresses, ROM or BIOS identities,
  save/input data, endpoint logs, commercial evidence, unexpected files, symlinks,
  duplicate names, unsafe modes, and secret-like values. Tests use synthetic
  canaries for every prohibited class.
- **Reason:** Automated publication removes the final human opportunity to notice
  accidental private material.
- **Alternatives considered:** A deny-list alone cannot provide a durable release
  privacy boundary.

### D7: Publication uses a private transactional stage but no human pause

- **Choice:** All workflow jobs default to read-only permissions. After every
  validation succeeds, a final job with only the required `contents`,
  `attestations`, and `id-token` write permissions downloads the canonical
  workflow artifact and runs `verify` before any API mutation. It creates a draft
  release, uploads exactly the seven assets, queries or downloads them back, verifies
  their names, counts, sizes, hashes, body identity, tag, and prerelease flag, then
  publishes automatically in the same job.

  Before publication, any failure removes the draft and leaves the workflow
  artifact/log evidence. If the publish response is uncertain, the job reads the
  release back: an exact complete public release is success; any conflicting public
  state is a loud failure and is never automatically deleted or overwritten.
- **Reason:** GitHub does not offer an atomic multi-asset public release API. A
  private draft provides the closest transactional boundary without becoming a
  manual gate.
- **Alternatives considered:** Uploading directly to a public release exposes
  partial state. A persistent draft awaiting review is not fully automated.

### D8: Tags and releases are immutable and reruns are idempotent

- **Choice:** Add a repository ruleset for `v*` tags that prevents update and
  deletion after creation. Use per-tag workflow concurrency without cancellation.
  A first run owns creation. A rerun after a successful release performs a
  read-only exact verification and succeeds only when the remote release matches
  the canonical locally rebuilt set; otherwise it fails. An unpublished orphan
  draft created by the same tag/run may be safely replaced only after its identity
  is verified.
- **Reason:** Reproducibility is meaningful only while source tags and published
  bytes remain stable.
- **Alternatives considered:** Force-replacing assets reproduces the v0.2.0
  correction ceremony and invalidates prior downloads. Cancel-in-progress can
  interrupt the publishing transaction.

### D9: Release notes are tracked input, not generated claims

- **Choice:** Each tag requires a reviewed, tracked release-notes input for that
  exact version. Static installation/provenance templates are version-neutral and
  rendered from canonical metadata. Missing notes or unresolved placeholders fail
  tag admission.
- **Reason:** Automation can render factual identities but should not invent
  compatibility or behavioral claims from commit messages.
- **Alternatives considered:** Automatically generated notes are convenient but can
  be incomplete or imply unreviewed product claims.

### D10: Runtime qualification remains proportionate and upstream of tagging

- **Choice:** The release workflow records the qualification decision associated
  with the tagged commit. Tooling-only changes rely on protected automated evidence;
  runtime-sensitive changes must complete their separately specified physical gate
  before the release tag is approved. The release workflow does not drive devices
  or fabricate a new gameplay requirement.
- **Reason:** Full release automation should remove mechanical work, not weaken or
  inflate product validation.
- **Alternatives considered:** Always requiring device playtests makes tooling
  releases ceremonial; never requiring them ignores runtime risk.

## Risks / Trade-offs

- **[Risk] A tag can be pushed before its intended source is ready.** → Mitigation:
  require an annotated canonical tag, protected-history reachability, exact-commit
  gates, version/release-note agreement, and an immutable tag ruleset; failure
  publishes nothing.
- **[Risk] GitHub-hosted runner images can change while retaining the same label.**
  → Mitigation: record the resolved runner image, pin downloaded toolchains/actions,
  require two same-run clean builds, and define runner image changes as a recorded
  provenance boundary. If byte reproducibility fails, no release is published.
- **[Risk] A packaging bug could publish private or inconsistent data automatically.**
  → Mitigation: allow-listed inputs, synthetic privacy canaries, deterministic
  fixture golden tests, internal/external manifests, and remote read-back before
  publication.
- **[Risk] GitHub may fail between publication and response.** → Mitigation: read
  back the public release and accept only the exact complete state; never delete a
  public release automatically.
- **[Trade-off] Two Android builds and the full suite increase release latency.** →
  Accepted because releases are infrequent and the cost replaces substantial
  manual identity/reproducibility work.
- **[Trade-off] Requiring tracked release notes retains one preparatory human task.**
  → Accepted because writing product claims is judgment, while pushing the approved
  tag remains the single release action.

## Migration Plan

1. Record the current v0.2.0 release contract and keep its version-specific tracked
   directory immutable.
2. Add version-neutral release templates, canonical metadata schema, packager, and
   deterministic unit/golden tests using synthetic release inputs.
3. Make the protected CI workflow reusable by the release workflow without changing
   its pull-request or protected-`master` behavior.
4. Add independent Android build/reproducibility jobs and artifact handoff with
   read-only permissions.
5. Add mocked publisher tests and the final least-privilege draft/read-back/publish
   job.
6. Add and independently inspect the immutable `v*` tag ruleset before enabling the
   release trigger.
7. Rehearse the full pipeline against a disposable non-public tag/release target,
   prove cleanup and retry behavior, then remove the rehearsal tag/release.
8. Enable the real tag trigger only after review and protected CI pass. The next
   approved release tag then publishes fully automatically.

Rollback before a public release consists of disabling the tag trigger and reverting
the workflow/tooling change. A failed unpublished transaction removes its draft and
retains workflow evidence. A successfully published release is immutable: recovery is
a new corrected version/tag, never silent asset replacement.

## Open Questions

None. The implementation must freeze exact action commit SHAs, provenance schema
field widths/types, release-note/template paths, and GitHub API/CLI versions before
recording golden fixtures, but those are implementation constants within the
decisions above rather than unresolved product choices.
