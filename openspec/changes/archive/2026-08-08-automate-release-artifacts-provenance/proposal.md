## Why

The v0.2.0 alpha release contents are sound, but manual building, packaging,
checksum generation, upload, correction, and publication are unnecessary work.
An approved maintainer tag should produce the complete Android prerelease without
another release ceremony.

## What Changes

**Trusted tag automation**
- From: A maintainer manually assembles and publishes release assets.
- To: One approved annotated semantic-version tag runs the complete release in a
  single trusted tag workflow with no later human gate.
- Trust model: Repository maintainers and the tags they deliberately push are
  trusted. Hostile maintainer-authored tags are out of scope.

**Reproducible artifacts and provenance**
- Build the Android ARM64 core twice in clean jobs and require identical bytes.
- Render one deterministic seven-asset public set with an exact archive inventory,
  provenance documents, and internal/standalone SHA-256 manifests.
- Reject undeclared or privacy-sensitive content before publication.

**Automatic fail-closed publication**
- Stage the verified set in a private draft, upload exactly those assets, read the
  remote state back, and publish automatically only when it matches.
- Reuse an exact complete public release read-only on rerun; preserve and report
  every conflicting state.

**Proportionate process**
- Remove the proposed protected controller, hostile-tag defenses, ruleset audit
  credential, public rehearsal repository, and signed-attestation requirement.
- Preserve the historical v0.2.0 package unchanged and require no additional
  physical gameplay for tooling-only changes.

## Capabilities

### New Capabilities

- `automated-release-provenance`: trusted tag admission, exact-source validation,
  reproducible Android builds, deterministic packaging, privacy checks,
  provenance/checksums, transactional publication, and idempotent reruns.

### Modified Capabilities

- `public-alpha-distribution`: future Android alpha releases use immutable
  tag-driven automation while v0.2.0 remains historical evidence.

## Impact

- Adds one tag-triggered release workflow and project-owned release tooling.
- Reuses the existing protected CI and Android build contracts.
- Updates release and maintainer guidance.
- Does not change GBA runtime, protocol, input, RTC, persistence, audio, video,
  scheduling, teardown, or support policy.
