## Why

The v0.2.0 alpha proved the release contents, but assembling and correcting them
manually was slow and exposed avoidable risks: stale identities, inconsistent
documentation, mismatched checksums, and partial uploads. An approved tag should
produce one reproducible, reviewable Android release without another manual
packaging exercise. This work also makes future releases easier to maintain
without creating a feedback programme or changing emulator behaviour.

## What Changes

**Release trigger and publication**
- From: A maintainer builds, assembles, checks, uploads, and publishes release
  assets through a largely manual procedure.
- To: Pushing a new approved annotated semantic-version tag automatically runs
  every validation, reproducibility, packaging, provenance, remote verification,
  and publication step.
- Reason: The tag is a sufficiently explicit release decision; every mechanical
  step after it should be deterministic and fail-closed.
- Impact: Non-breaking release-engineering change; no runtime or wire change.

**Artifact identity and reproducibility**
- From: Release identities and manifests are reconstructed for each release.
- To: A tested packaging tool renders a canonical release set from tracked
  templates, exact tag/source/toolchain inputs, and two byte-identical clean
  Android builds.
- Reason: Repeated tagged inputs should produce the same executable and package
  payloads and explain the separate signed-attestation boundary.
- Impact: Future Android alpha releases use a fixed manifest, archive, checksum,
  privacy, and provenance contract.

**Publication safety**
- From: Asset replacement and final consistency checking depend on manual steps.
- To: A least-privilege publication job stages the verified set in a private
  draft, validates the remote names, sizes, hashes, identities, and release body,
  then publishes automatically. Existing or conflicting releases are never
  overwritten.
- Reason: Full automation must not create mixed or partially public releases.
- Impact: Tag and release immutability become enforced project policy.

The change also updates the roadmap from a feedback-oriented “supportable alpha”
to the approved neutral-feedback “maintainable alpha” direction and defers the
diagnostic-bundle issue without forbidding unsolicited reports.

## Capabilities

### New Capabilities

- `automated-release-provenance`: Tag admission, protected evidence, reproducible
  Android builds, deterministic packaging, provenance, privacy, transactional
  publication, retry behavior, and release verification.

### Modified Capabilities

- `public-alpha-distribution`: Future public alpha releases move from the special
  v0.2.0 manual correction model to immutable, tag-driven automated publication
  while preserving coherent identity and privacy requirements.

## Impact

- Adds a tag-triggered GitHub Actions release workflow with a narrowly privileged
  publication job and pinned third-party actions/toolchains.
- Adds deterministic release-building and validation tooling with mocked
  publication tests.
- Adds tracked, version-aware release metadata/templates and a machine-readable
  provenance manifest while retaining the historical v0.2.0 package unchanged.
- Adds an immutable `v*` tag ruleset and documents the tag as the release action.
- Updates release, provenance, roadmap, milestone, and maintainer guidance.
- Reuses the existing Android ARM64 build, fixture, boundary, and protected test
  contracts; no GBA execution, protocol, save, input, audio, or presentation code
  changes.
