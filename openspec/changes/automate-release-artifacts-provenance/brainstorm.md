# Automated release artifacts and provenance — brainstorm capture

## Background

The v0.2.0 public alpha proved that the Android ARM64 core, redistributable
fixtures, offline documentation, provenance notice, archive, and checksum
manifests can form one coherent release. It also proved that assembling and
correcting that set manually is unnecessarily slow and exposes too many
opportunities for stale identities, mismatched checksums, incomplete uploads,
or a release body that disagrees with its assets.

The next increment serves the maintainer rather than creating a user-feedback
programme. The repository remains neutral toward unsolicited feedback, and the
release workflow does not depend on the separately deferred diagnostic-bundle
issue.

## Decision chain

1. The source of a release is an existing, deliberately created annotated
   semantic-version tag on the protected project history.
2. Creating and pushing that approved tag is the maintainer's release decision.
   There is no later human approval, draft-release pause, manual asset upload,
   or Publish click.
3. Every action after the tag push is automated: source validation, protected
   test evidence, clean builds, reproducibility comparison, identity checks,
   deterministic packaging, provenance and checksum generation, private
   staging, remote verification, and public release publication.
4. The workflow is fail-closed. A missing or failed prerequisite, identity
   mismatch, non-reproducible output, malformed package, upload mismatch, or
   pre-existing release aborts publication. No partial public release is an
   acceptable result.
5. A temporary GitHub draft may be used transactionally inside the workflow,
   but it is not a human gate. Automation either verifies and publishes the
   complete release or removes/retains only non-public diagnostic state under a
   defined cleanup rule.
6. Future releases are immutable. The special in-place documentation correction
   used for v0.2.0 remains historical evidence, not the normal automated model.

## Approaches considered

### Selected: untrusted tag intake plus a protected release controller

An annotated `v*` tag triggers a read-only intake workflow. Completion of that
intake wakes a separate `workflow_run` controller whose definition and release
authority come from protected default-branch code, not from the tag being
validated. The controller independently resolves and admits the tag, then runs
protected evidence, two clean builds, deterministic packaging, attestation and
transactional publication. Candidate source is checked out only after admission
and is treated as build/data input; trusted release tooling comes from the
controller checkout.

This keeps the trust boundary enforceable: the tag is still the sole human
release command, but tag-controlled YAML cannot grant itself publication
authority or replace its own ancestry check.

### Rejected: a privileged workflow sourced from the candidate tag

Job-level permissions inside one tag-sourced workflow do not establish a trust
boundary because GitHub evaluates that workflow from the tag's commit. An
off-history tag can replace the admission and publisher jobs together. The
protected controller accepts the extra event-correlation work because default-
branch ownership is required for real privilege separation.

### Rejected: publish from a version-file change on `master`

This is convenient but makes an ordinary merge capable of becoming a release.
An explicit annotated release tag is a clearer and safer maintainer action.

## Agreed design

- Accept only a new annotated semantic-version tag whose target is the intended
  protected project history and whose version agrees with the generated release
  metadata.
- Reject lightweight, malformed, moved, reused, or already-published tags.
- Pin the runner inputs that affect the Android build, including the NDK/toolchain
  identity and downloaded dependency hashes.
- Build the Android ARM64 core in two independent clean jobs and require
  byte-identical executable output before packaging.
- Verify ELF architecture, embedded full source commit, product version, canonical
  GBA Wi-Fi Link product identity, and the versioned protocol identity.
- Generate the archive and standalone assets from tracked templates plus canonical
  tag/build metadata. Normalize file ordering, timestamps, ownership, permissions,
  compression settings, and generated serialization so the release set is
  reproducible.
- Include the core, redistributable fixtures, install/usage guide, source and
  provenance notice, MPL-2.0 licence, internal build provenance and checksums,
  the archive, standalone release provenance, and a standalone checksum manifest.
  The provenance/checksum dependency order must avoid self-referential hashes.
- Keep raw ROM, BIOS, save, input, private path/address, endpoint-log, and commercial
  evidence out of every artifact and provenance field.
- Upload the canonical set as a workflow artifact for audit, then let a separate
  `contents: write` publication job create a private draft, upload exactly that
  set, download or query it back, verify names/sizes/hashes/body metadata, and
  publish automatically.
- Use per-tag concurrency and never overwrite an existing tag or release. Before
  rebuilding, a rerun after successful publication downloads and validates the
  original public bytes, provenance, body, tag and attestations. It exits without
  mutation only when that retained first-run evidence is complete and coherent;
  volatile new run/job IDs are never used to regenerate a supposedly identical
  release.
- Treat `v0.x` releases as prereleases until a separately reviewed policy changes
  that classification.
- Do not require new physical gameplay merely for release-tooling changes. Runtime
  changes retain their existing proportionate qualification rules before their
  release tag is approved.

## Testing and evidence

- Unit-test tag parsing, metadata canonicalization, deterministic timestamps,
  archive membership, checksum scopes, provenance schema, privacy exclusions,
  duplicate/retry rules, and release-body rendering.
- Rebuild the same synthetic tag inputs twice and require byte-identical core and
  package outputs.
- Exercise publication through a mocked GitHub API/CLI boundary covering success,
  partial upload, hash mismatch, existing release, rerun, and cleanup.
- Run the existing protected normal, sanitizer, complete-suite, fixture, helper,
  boundary, and Android-build gates.
- Prove the privileged publication job consumes only the previously verified
  canonical artifacts and has no build step.
- Perform one end-to-end rehearsal against an explicitly named disposable public
  repository containing synthetic/re-distributable material only. Generate a
  narrowly transformed rehearsal tree whose repository and signer identities are
  fixed to that one target, prove the production tree remains hard-coded to the
  canonical repository, record the public transparency-log consequence, then
  delete the rehearsal release, tag and repository. The first real release
  remains fully automatic from its approved tag.

## Scope boundary

This change automates Android ARM64 alpha releases and their provenance. It does
not add stable promotion, additional platforms, signing infrastructure beyond
available GitHub attestations, diagnostic collection, automatic version choice,
runtime behavior, or a user-feedback programme.
