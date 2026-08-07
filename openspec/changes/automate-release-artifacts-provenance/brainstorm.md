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

### Selected: one tag-triggered, fail-closed release workflow

An annotated `v*` tag triggers a release workflow. Unprivileged jobs validate
the tag and source, run or bind the required protected evidence, build twice in
clean jobs, compare the resulting core and package payloads, and produce a
canonical release set. A final least-privilege publication job stages that exact
set privately, verifies the remote draft, and publishes it automatically.

This keeps the trust boundary understandable: the tag is the release command,
build jobs cannot publish, and the publication job cannot invent or rebuild
assets.

### Rejected: chain publication from a separate completed CI workflow

Separating build and publication workflows can reduce permission scope, but it
adds cross-workflow artifact trust, run-selection, and event-correlation
complexity. The same separation can be achieved inside one workflow with job
permissions and immutable artifact identities.

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
- Use per-tag concurrency and never overwrite an existing tag or release. A rerun
  after successful publication verifies the existing release and exits without
  mutation only when it is byte-identical; otherwise it fails.
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
- Perform one end-to-end rehearsal against a disposable non-public tag/repository
  or equivalent isolated release target, then delete the rehearsal release and
  tag. The first real release remains fully automatic from its approved tag.

## Scope boundary

This change automates Android ARM64 alpha releases and their provenance. It does
not add stable promotion, additional platforms, signing infrastructure beyond
available GitHub attestations, diagnostic collection, automatic version choice,
runtime behavior, or a user-feedback programme.
