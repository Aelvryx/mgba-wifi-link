# Public alpha sharing readiness

## Background

GBA Wi-Fi Link already has a public repository, a clearly labelled Android
ARM64 alpha release, protected CI, integrity hashes, support and security
policies, issue forms, an honest compatibility matrix, and substantial
automated and human playtesting. The project is ready to be shown to other
people as an experimental alpha; it does not need another emulator feature,
architectural review, or long commercial-game qualification merely to share
the link.

The public-readiness audit found one concrete mismatch. The downloadable
v0.2.0 assets have valid checksums, but the standalone
`INSTALL-AND-USAGE.md` embedded in the release and bundle predates the public
documentation privacy pass. It still names private qualification devices.
The release bundle also lacks an embedded licence and concise source/provenance
pointer, even though the release page already identifies the repository,
source commit, and artifact hashes.

## Decision chain

### Q1: What audience are we preparing for?

The immediate audience is friends, enthusiasts, and technically comfortable
alpha testers following a public GitHub link. This is not a declaration of
general stability or universal game compatibility.

### Q2: Must v0.2.1 supportability work finish first?

No. The sanitized diagnostic bundle, automated release provenance, upstream
refresh, and decoder fuzzing remain the planned v0.2.1 outcomes. They improve
supportability and repeatability, but holding the first honest alpha link until
all four finish would confuse a roadmap milestone with a basic publication
gate.

The sanitized diagnostic bundle is especially valuable before a broad call
for reports from strangers, but it is not required before sharing the current
alpha with a small audience under the existing privacy-aware support guidance.

### Q3: How should the stale v0.2.0 assets be corrected?

Three approaches were considered:

1. **Repack the existing prerelease in place.** Keep the exact core and CC0
   fixture bytes, replace the guide with a tracked neutral guide, add licence
   and source/provenance material, regenerate the archive and checksums, and
   disclose that the prerelease assets received a documentation-only repack.
2. **Publish a documentation-only replacement tag.** This preserves every old
   asset forever but creates a new software version despite no binary change
   and makes an already small alpha history harder to understand.
3. **Wait for v0.2.1.** This avoids mutating a prerelease but unnecessarily
   blocks useful early feedback on unrelated supportability work.

Approach 1 is selected. v0.2.0 is explicitly a prerelease with negligible
existing adoption, so a transparent documentation-only correction is simpler
and more honest than manufacturing a new product version. The release notes
must preserve the original core identity and record the new bundle/checksum
identities rather than implying the entire release was rebuilt.

### Q4: How do we prevent the offline guide drifting again?

The install-and-usage guide becomes a tracked release input rather than a
one-off file edited outside the repository. Its public setup, limitations,
privacy, support, and compatibility wording must agree with the current README
and operating guide. The release archive includes the tracked guide unchanged.

This change may document the exact repack commands and validate archive
contents, but it does not implement the general tag-to-release automation
planned by issue #21.

### Q5: What evidence is proportionate?

Artifact evidence must prove:

- the standalone core is byte-identical to the published v0.2.0 core;
- both CC0 fixtures are byte-identical;
- the archive contains only the intended files;
- all published checksums match;
- the guide and bundled text contain no private device names, serials,
  addresses, paths, commercial content, or private test data;
- the release page records the exact source commit and corrected artifact
  hashes.

Human evidence is one time-boxed cold-reader rehearsal in which a person who
did not author the instructions follows only the public release material to
install the core and establish one short link session. The maintainer may
observe and record friction but should not silently supply missing steps. A
redistributable fixture is preferred; a brief game connection is acceptable
when both participants already own the content. This is onboarding validation,
not another performance or compatibility gate.

### Q6: What is explicitly not required?

This change does not require:

- emulator, protocol, latency, save, RTC, SIO, presentation, or audio changes;
- another Mario Kart or Four Swords soak;
- a minimum number of compatible commercial titles;
- Single-Pak, four-player, RFU, reconnection, or additional platforms;
- the v0.2.1 diagnostic, automation, upstream, or fuzzing work;
- signed tags, Discussions, or a second maintainer approval requirement.

## Agreed design

Add a tracked, neutral offline release guide plus small licence and source
provenance files. Reconstruct the v0.2.0 Android ARM64 archive from the original
byte-identical core and fixtures and the new tracked text files. Generate a new
complete checksum manifest, verify it and the archive shape locally, and update
the existing prerelease assets and release description with a clear
documentation-only repack note.

Then run a single cold-reader installation/link rehearsal, record only a brief
sanitized outcome and any actionable documentation friction, and correct only
real onboarding defects. Passing this gate makes the repository comfortable to
share as an experimental Android ARM64 two-player Multi-Pak alpha. Broader
public support remains the v0.2.1 roadmap rather than being smuggled into this
change.
