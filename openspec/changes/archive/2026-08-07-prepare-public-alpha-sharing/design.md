## Context

The public repository already presents GBA Wi-Fi Link as an independent,
experimental Android ARM64 alpha with explicit scope, limitations, privacy
guidance, support routing, protected CI, and reproducible artifact identities.
The v0.2.0 GitHub prerelease contains a verified core, two CC0 fixtures, a
standalone guide, a zip bundle, and checksums.

The downloadable guide is the remaining inconsistency. It was produced before
the repository's public-language pass and retains qualification-specific device
names. The archive also assumes that a reader will visit GitHub for licence and
source context. The intended audience is a small public alpha cohort, not users
being promised stable or universal compatibility.

The original v0.2.0 identities that this change must preserve are:

| Item | Immutable identity |
| --- | --- |
| Source tag | `v0.2.0` |
| Source commit | `86cc1c26eaf26b5024689c5cb723aa6152efb795` |
| Android core | `b16873176e358883acc7e7cb8f0312b9a275610a6a9e84c87e092accc3748910` |
| `gba-link-test.gba` | `2f662e4bcf2ac81c438ae5eacc786b2d2984c00807d3656b4990da66a99edc13` |
| `gba-link-continuous.gba` | `c1fe01752d4f5863d6e3e1a9866b061aaadf2927ccc3df31ba9ecbf4bc68fe9d` |

The guide, archive, and checksum identities will legitimately change. No
production source or binary will be rebuilt.

## Goals / Non-Goals

**Goals:**

- Make every v0.2.0 download agree with the current neutral public
  presentation.
- Make the archive understandable offline by including installation guidance,
  licence text, and an exact source/provenance pointer.
- Preserve the original core, fixture, tag, and source-commit identities.
- Make the corrected archive shape and checksum relationships mechanically
  verifiable.
- Define a proportionate onboarding-feedback path for limited alpha sharing
  without manufacturing another staged qualification exercise.
- State the exact boundary between a shareable experimental alpha and the later
  v0.2.1 supportability milestone.

**Non-Goals:**

- Changing or rebuilding emulator, protocol, fixture, save, latency, RTC, SIO,
  audio, video, or lifecycle behavior.
- Implementing issue #20 diagnostics, issue #21 general release automation,
  issue #22 upstream refresh, or issue #23 decoder fuzzing.
- Requalifying commercial games, running a soak, or expanding the compatibility
  matrix.
- Publishing a new product version, stable release, or compatibility promise.
- Adding release signing, a support forum, or additional maintainer policy.

## Decisions

### D1: Correct the existing prerelease in place

- **Choice:** Retain tag `v0.2.0`, preserve its source and binary identities,
  and replace only the stale documentation, archive, checksum manifest, and
  release description.
- **Rationale:** This is a transparent correction to an explicitly experimental
  prerelease with negligible adoption, not a software change.
- **Alternatives considered:** A new documentation-only tag would imply a new
  binary version; waiting for v0.2.1 would make unrelated supportability work a
  prerequisite for useful alpha feedback.

### D2: Track release-specific text inputs

- **Choice:** Store the corrected guide and source/provenance notice under
  `packaging/gba-wifi-link/v0.2.0/`. Copy the repository root `LICENSE` into the
  archive during staging.
- **Rationale:** The exact public material becomes reviewable and reconstructible
  without pretending this one repair is the general release automation promised
  by issue #21. A version-specific directory can remain immutable after the
  correction.
- **Alternatives considered:** Generating the guide from README would couple an
  immutable old release to changing current prose; retaining an off-repository
  guide would recreate the drift that caused this change.

### D3: Use two checksum scopes

- **Choice:** The archive contains an internal `SHA256SUMS` covering every
  payload member except that manifest itself. The standalone release
  `SHA256SUMS` covers every uploaded binary, fixture, guide, and archive asset
  except the standalone manifest itself.
- **Rationale:** A downloaded archive can be verified after extraction, while
  the release page can independently verify each downloadable artifact.
- **Alternatives considered:** Hashing only the archive hides member identity;
  hashing only members does not identify the archive actually published.

### D4: Freeze executable and fixture bytes

- **Choice:** Build the corrected package from downloaded and hash-verified
  v0.2.0 artifacts. Reject the operation if the core or either fixture differs
  from the immutable baseline above.
- **Rationale:** Byte identity is the decisive proof that the correction cannot
  affect gameplay and does not need runtime requalification.
- **Alternatives considered:** Rebuilding the core from the tag could be
  reproducible but would create needless uncertainty and would turn the change
  into a new release-candidate exercise.

### D5: Publish through a recoverable staging sequence

- **Choice:** Before mutating GitHub, preserve the original release body and all
  original assets locally; construct and verify the complete corrected set;
  temporarily make the prerelease non-public if supported; replace same-named
  assets; update the body with the repack disclosure and exact hashes; then
  redownload and verify the public result before restoring normal visibility.
- **Rationale:** Users must never be intentionally directed to a mixed set of
  old and new checksums. A local snapshot gives a real rollback path.
- **Alternatives considered:** Replacing assets one by one while public creates
  an avoidable inconsistent window. Uploading duplicate suffixed names leaves
  users to guess which artifact is authoritative.

### D6: Let limited public use provide the onboarding signal

- **Choice:** Do not require a staged cold-reader rehearsal before sharing. The
  corrected guide receives normal review, every published artifact is
  independently verified, and existing playtesting establishes that the
  documented path reaches working sessions. Limited alpha users then exercise
  the real onboarding path and may report concrete documentation friction
  through the repository's normal issue route.
- **Rationale:** A contrived pre-sharing exercise adds coordination ceremony but
  little confidence beyond the evidence already available. Real limited-alpha
  use is the more representative onboarding test.
- **Alternatives considered:** A twenty-minute unfamiliar-reader gate was
  designed and rejected as disproportionate; another commercial playtest would
  test compatibility rather than onboarding.

### D7: Define two publication thresholds

- **Choice:** The corrected, independently verified bundle authorizes sharing
  with friends, enthusiasts, and technical alpha testers. A broad campaign
  inviting support requests from strangers should additionally wait for the
  sanitized diagnostic bundle in issue #20.
- **Rationale:** Existing issue forms and privacy guidance are adequate for a
  small alpha cohort, while scalable public support benefits materially from
  safe one-command evidence collection.
- **Alternatives considered:** Treating issue #20 as a blocker for all sharing
  overstates the support burden; ignoring it before broad solicitation would
  invite low-quality or privacy-sensitive reports.

## Risks / Trade-offs

- **[Risk]** Replacing assets under the same tag invalidates previously recorded
  guide and archive hashes. **Mitigation:** preserve the core/fixture hashes,
  disclose the documentation-only repack prominently, publish the new hashes,
  and retain the old asset snapshot and identities in the readiness record.
- **[Risk]** GitHub asset replacement can temporarily expose a mixed set.
  **Mitigation:** stage and verify everything first, hide the prerelease during
  replacement where supported, and verify the public downloads before sharing.
- **[Risk]** A tracked offline guide duplicates parts of README and may drift in
  later releases. **Mitigation:** make current-document agreement an explicit
  review and test requirement; issue #21 will later own generalized assembly.
- **[Risk]** A limited-alpha user encounters an undocumented step. **Mitigation:**
  treat a reproducible report as a documentation defect, correct the tracked
  guide, and transparently repack the affected documentation assets.
- **[Trade-off]** In-place correction does not preserve the original archive at
  its public URL. **Accepted because:** the old archive is retained privately
  for rollback, its identity is recorded, and creating a fake software version
  would be more confusing for this low-adoption prerelease.

## Migration Plan

1. Snapshot the existing release body, asset metadata, and every downloadable
   asset; verify them against the original checksum manifest.
2. Add and review the version-specific neutral guide and source/provenance
   notice. Confirm they agree with current README, support, security, and
   operating guidance.
3. Stage the immutable core and fixtures with the tracked text and root licence.
   Generate the internal manifest and deterministic archive, then the external
   release checksum manifest.
4. Run archive-shape, checksum, identity, privacy, content, and source-pointer
   checks locally. No GitHub asset changes occur unless all pass.
5. Replace the v0.2.0 documentation/archive/checksum assets and update the
   release description with the correction disclosure and new hashes.
6. Redownload every public asset into a clean directory and rerun the complete
   verification. If this fails, restore the saved original body and assets and
   keep the release unadvertised until resolved.
7. Record the deliberate decision that no separate cold-reader gate is needed;
   route concrete limited-alpha onboarding feedback through the existing issue
   process.
8. Record the final readiness result and begin limited public-alpha sharing.

Rollback does not change the `v0.2.0` tag or source. The preserved original
release body and assets can be restored exactly if publication fails before the
corrected set is verified.

## Open Questions

None. General release automation and broader diagnostic support remain bounded
by roadmap issues #21 and #20 respectively.
