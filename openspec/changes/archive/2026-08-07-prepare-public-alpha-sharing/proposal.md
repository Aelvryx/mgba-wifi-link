## Why

GBA Wi-Fi Link is technically and operationally ready to be shared as an
experimental alpha, but the published v0.2.0 bundle contains an offline guide
from before the repository's privacy and product-language pass. A newcomer can
therefore download material that does not match the polished public repository.
Correcting and independently verifying that artifact closes the real
publication gap without holding the alpha behind unrelated v0.2.1
supportability work or an artificial staged onboarding exercise.

## What Changes

**Published v0.2.0 bundle**
- From: A valid, hash-identified prerelease whose bundled guide contains stale
  private qualification-device terminology and whose archive does not carry a
  self-contained licence and source pointer.
- To: A transparently repacked prerelease retaining the exact core and CC0
  fixture bytes, with a tracked neutral guide, licence, source/provenance
  pointer, regenerated archive and checksums, and an explicit docs-only repack
  note.
- Reason: The downloadable product must match the repository's current public
  presentation and remain understandable when read offline.
- Impact: Non-breaking documentation and packaging correction; emulator and
  protocol bytes do not change.

**Public onboarding evidence**
- From: Installation and operation have been exercised primarily by people
  involved in development and qualification.
- To: The reviewed and independently verified public release may be shared with
  a limited alpha audience; their real use becomes the onboarding signal rather
  than a contrived pre-sharing rehearsal.
- Reason: Existing playtesting already proves the documented path reaches
  working sessions, while real users are more representative than another
  maintainer-orchestrated gate.
- Impact: Concrete documentation friction is corrected through normal public
  feedback; no additional performance, compatibility, or commercial-game gate
  is introduced.

The change does not implement diagnostics, general release automation, an
upstream refresh, decoder fuzzing, new platforms, new multiplayer modes, or any
runtime behavior.

## Capabilities

### New Capabilities

- `public-alpha-distribution`: Defines coherent, privacy-safe, independently
  verifiable public alpha artifacts and a proportionate onboarding-feedback
  policy.

### Modified Capabilities

None.

## Impact

- Adds a tracked offline installation-and-usage release input and concise
  source/provenance material.
- Reconstructs and verifies the existing GitHub v0.2.0 prerelease archive and
  checksum manifest while preserving the original core and fixture identities.
- Updates the GitHub release assets and description through an explicitly
  recorded publication step.
- Records that limited public-alpha feedback, not a staged cold-reader
  rehearsal, owns post-publication onboarding validation.
- Does not change production source, public APIs, Netpacket compatibility,
  emulator state, saved data, or the qualified Android binary.
