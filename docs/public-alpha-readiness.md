# v0.2.0 Public Alpha Readiness

## Scope

This record covers a documentation-only repack of the existing `v0.2.0`
public-alpha assets.  It prepares the Android ARM64 core, the two CC0 fixture
ROMs, corrected guidance, a deterministic bundle, and checksum manifests for
replacement on the existing prerelease.  It does not rebuild executable code,
change the tag, or publish a new release.

## Preserved source and executable identities

The source commit remains `86cc1c26eaf26b5024689c5cb723aa6152efb795`.
The Android ARM64 core remains
`b16873176e358883acc7e7cb8f0312b9a275610a6a9e84c87e092accc3748910`.
The `gba-link-test.gba` fixture remains
`2f662e4bcf2ac81c438ae5eacc786b2d2984c00807d3656b4990da66a99edc13`, and
the `gba-link-continuous.gba` fixture remains
`c1fe01752d4f5863d6e3e1a9866b061aaadf2927ccc3df31ba9ecbf4bc68fe9d`.
These payloads were copied from the verified rollback snapshot and their bytes
were not changed.

## Documentation-only repack

The original guide SHA-256 was
`69eafbef57876bedc8acb17163886de3f587f19a42fa00d6440d3838b4f085f1` and the
original bundle SHA-256 was
`8ba4c62975f11bb8a52109a2538d811fb868d9e8ebf31aa7f09d9e18808dd4fc`.
The corrected bundle replaces the guide and adds the current
`SOURCE-AND-PROVENANCE.md` and `LICENSE`; its executables and fixtures are the
preserved payloads above.

## Corrected artifact identities

The corrected standalone `INSTALL-AND-USAGE.md` SHA-256 is
`880190bf1b65f3f44690163e2374f79f813a274e31547dbced4920e1fb4e64e8`.
The corrected `mgba-gba-wifi-link-v0.2.0-android-arm64.zip` SHA-256 is
`167019d858e811cc35abd70a5300fbb24caf77c32b76e6b7efc3ecaca7cc22f5`.
The corrected standalone `SHA256SUMS` manifest SHA-256 is
`1a777399d6fc1e621f381e4e5a251f6b1164d8a82a1cc53dd88631c3ffdece5d`.

The standalone manifest lists the core, both fixtures, corrected guide, and
corrected bundle.  The bundle contains those payloads plus
`SOURCE-AND-PROVENANCE.md`, `LICENSE`, and an internal `SHA256SUMS` manifest.

## Reconstruction and verification

Two independent clean staging roots were populated in a fixed member order.
Each archive used DEFLATE level 9, Unix mode `100644`, no extra fields, and
the fixed ZIP timestamp `2026-08-05 00:00:00`.  Their SHA-256 values matched
exactly and `cmp` reported no byte differences.

A third clean directory verified the standalone manifest with `sha256sum -c`.
After extracting the corrected bundle, its internal manifest also verified
with `sha256sum -c`.  The extracted root contained exactly
`INSTALL-AND-USAGE.md`, `LICENSE`, `SHA256SUMS`,
`SOURCE-AND-PROVENANCE.md`, `gba-link-continuous.gba`,
`gba-link-test.gba`, and `mgba_libretro_android.so`.

## Corrected public prerelease

On 2026-08-07 the release at
<https://github.com/Aelvryx/mgba-wifi-link/releases/tag/v0.2.0> was temporarily
held as a draft while its documentation-only assets were replaced. It is again
published as a prerelease with exactly six assets:

- `mgba_libretro_android.so` —
  `b16873176e358883acc7e7cb8f0312b9a275610a6a9e84c87e092accc3748910`
- `gba-link-test.gba` —
  `2f662e4bcf2ac81c438ae5eacc786b2d2984c00807d3656b4990da66a99edc13`
- `gba-link-continuous.gba` —
  `c1fe01752d4f5863d6e3e1a9866b061aaadf2927ccc3df31ba9ecbf4bc68fe9d`
- `INSTALL-AND-USAGE.md` —
  `880190bf1b65f3f44690163e2374f79f813a274e31547dbced4920e1fb4e64e8`
- `mgba-gba-wifi-link-v0.2.0-android-arm64.zip` —
  `167019d858e811cc35abd70a5300fbb24caf77c32b76e6b7efc3ecaca7cc22f5`
- `SHA256SUMS` —
  `1a777399d6fc1e621f381e4e5a251f6b1164d8a82a1cc53dd88631c3ffdece5d`

A fresh authenticated download from the published release verified the
standalone manifest and the archive's internal manifest. The downloaded guide,
archive, and manifest were byte-identical to the independently reviewed local
release set. The release body carries a prominent documentation-only correction
note. The source tag, Android core, and both fixtures are unchanged.

The private rollback snapshot remains retained until the public-alpha sharing
change is complete. It contains the original release body and assets in case a
later release-management problem requires recovery.

## Second documentation-only correction

Later on 2026-08-07, review found that the public guide and `SUPPORT.md` still
asked users to provide endpoint logs, contradicting the recorded privacy
boundary and the future sanitized-diagnostic workflow in issue #20. The guide
now requests only bounded, non-sensitive report fields and explicitly forbids
public endpoint, core, or RetroArch logs until that separate workflow exists.
`SUPPORT.md` applies the same public-reporting boundary.

Before replacement, the complete current live prerelease body and six assets
were downloaded into a new private rollback snapshot. The prior standalone
guide, bundle, and manifest hashes were respectively
`880190bf1b65f3f44690163e2374f79f813a274e31547dbced4920e1fb4e64e8`,
`167019d858e811cc35abd70a5300fbb24caf77c32b76e6b7efc3ecaca7cc22f5`, and
`1a777399d6fc1e621f381e4e5a251f6b1164d8a82a1cc53dd88631c3ffdece5d`.

The revised standalone guide SHA-256 is
`93357893a8b967cb0961a6818e2b6b9b957ea92bef3eed1d00bfe9846f3970dd`, the
revised bundle SHA-256 is
`0bdf9546a7e8d25e8064d0db3849c0d68c627fd03d5da35606dc61e1c84c851f`, and the
revised standalone manifest SHA-256 is
`13222ac91cffe21a5286aa7f53af79b6d9e571753fc9a3d21ef4d342e67f4669`.
The source tag, source commit, Android core, and both fixture hashes remained
exactly unchanged.

The prerelease was temporarily held as a draft for replacement of only those
three mutable assets and its updated disclosure. A fresh authenticated download
after republishing verified the revised standalone manifest, the extracted
archive's internal manifest, its exact seven-member allow-list, the updated
log-reporting prohibition, and the preserved core and fixture hashes. The
release is again a published prerelease with exactly six assets.

## Onboarding feedback policy

A separate staged cold-reader rehearsal is deliberately waived for this
limited experimental alpha. The public guide has been reviewed, the exact
published downloads have been independently verified, and existing playtesting
already demonstrates working sessions through the documented installation and
trusted-network connection path. Requiring another maintainer-orchestrated
exercise would add ceremony without materially changing the sharing decision.

Limited public-alpha users now provide the representative onboarding signal.
Concrete missing or ambiguous documentation should be reported through the
repository's normal issue path. A reproducible documentation defect updates
the tracked guide and any affected release documentation assets through the
same verified repack process; it does not create a speculative pre-sharing
gameplay or performance gate.

Any future onboarding report or correction preserves the existing privacy
boundary: do not publish device identifiers, network addresses, private paths,
raw logs, commercial content, save data, or controller-input history.

## Sharing boundary

The public package may contain only the Android ARM64 core, the two
redistributable fixture ROMs, corrected documentation, the MPL-2.0 licence,
and checksums.  Do not share commercial ROMs, BIOS files, copyrighted extracts,
private save data, endpoint logs, addresses, controller-input histories, or
private qualification evidence.

## Implementation-complete handoff

On 2026-08-07, a clean authenticated download of every public v0.2.0 asset
passed the standalone manifest, and the extracted bundle passed its internal
manifest. The release remains an experimental Android ARM64 alpha for exactly
two players using GBA Multi-Pak over a trusted local LAN or direct Android
hotspot with the same effective ROM bytes on both devices. The published guide
retains the unsupported-feature, privacy, and independent-fork limitations.

The separate cold-reader rehearsal is waived under the proportionate
onboarding policy above; limited alpha users supply the real documentation
signal through the normal issue path. Linking the repository or prerelease to
friends, enthusiasts, and technically comfortable testers is therefore open.
This is not a broad troubleshooting campaign: active solicitation of reports
from a broad unfamiliar audience remains gated on the sanitized diagnostic
workflow tracked in issue #20.

This record hands the implemented change to the schema-controlled verification,
retrospective, capability-sync/archive, final-check, PR-readiness, and protected
merge sequence. No additional device soak or commercial-game run is required
for this documentation-only correction.
