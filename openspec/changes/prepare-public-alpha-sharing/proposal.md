## Why

GBA Wi-Fi Link is technically and operationally ready to be shared as an
experimental alpha, but the published v0.2.0 bundle contains an offline guide
from before the repository's privacy and product-language pass. A newcomer can
therefore download material that does not match the polished public repository.
Correcting that artifact and proving that an unfamiliar reader can follow the
instructions closes the real publication gap without holding the alpha behind
unrelated v0.2.1 supportability work.

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
- To: One time-boxed cold-reader rehearsal proves that an unfamiliar person can
  install the exact public artifact and establish a short link session using
  only the published instructions.
- Reason: A public link is only useful when its intended alpha audience can get
  started without undocumented maintainer knowledge.
- Impact: Documentation friction may be corrected; this is not another
  performance, compatibility, or commercial-game qualification gate.

The change does not implement diagnostics, general release automation, an
upstream refresh, decoder fuzzing, new platforms, new multiplayer modes, or any
runtime behavior.

## Capabilities

### New Capabilities

- `public-alpha-distribution`: Defines coherent, privacy-safe, independently
  verifiable public alpha artifacts and a proportionate newcomer onboarding
  gate.

### Modified Capabilities

None.

## Impact

- Adds a tracked offline installation-and-usage release input and concise
  source/provenance material.
- Reconstructs and verifies the existing GitHub v0.2.0 prerelease archive and
  checksum manifest while preserving the original core and fixture identities.
- Updates the GitHub release assets and description through an explicitly
  recorded publication step.
- Adds sanitized evidence of one cold-reader installation and short connection
  rehearsal.
- Does not change production source, public APIs, Netpacket compatibility,
  emulator state, saved data, or the qualified Android binary.
