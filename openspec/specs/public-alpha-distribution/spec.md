# Public Alpha Distribution Specification

## Purpose

Define how an experimental GBA Wi-Fi Link alpha is packaged, verified,
corrected, and shared with a limited audience while preserving executable
identity, privacy, provenance, and honest support boundaries.
## Requirements
### Requirement: Public alpha artifacts have one coherent identity

The published v0.2.0 Android ARM64 prerelease SHALL identify one source tag and
commit, one exact Android core, and one exact copy of each redistributable
fixture across its release description, standalone assets, checksum manifests,
archive, and offline documentation. The release SHALL remain explicitly
labelled as an experimental, independent alpha rather than an upstream mGBA or
stable release.

#### Scenario: Reader compares public surfaces
- **WHEN** a reader compares the repository landing page, release description,
  standalone guide, archive contents, and source/provenance notice
- **THEN** every surface names the same v0.2.0 source and artifact identities and
  the same experimental Android ARM64 two-player Multi-Pak scope

#### Scenario: Artifact identity conflicts
- **WHEN** a staged guide, manifest, archive, or release description identifies
  a different source, core, fixture, platform, or product scope
- **THEN** publication fails before any corrected asset is advertised

### Requirement: The docs-only correction preserves executable evidence

The corrected v0.2.0 package MUST preserve byte-for-byte the previously
published Android core and both CC0 fixture ROMs. Their SHA-256 values SHALL
remain respectively
`b16873176e358883acc7e7cb8f0312b9a275610a6a9e84c87e092accc3748910`,
`2f662e4bcf2ac81c438ae5eacc786b2d2984c00807d3656b4990da66a99edc13`,
and `c1fe01752d4f5863d6e3e1a9866b061aaadf2927ccc3df31ba9ecbf4bc68fe9d`.
Any executable or fixture change SHALL require a separately reviewed release
rather than this documentation-only correction.

#### Scenario: Immutable payloads match
- **WHEN** the corrected archive and standalone assets are staged
- **THEN** the core and fixture hashes exactly match the three immutable
  baselines before packaging or publication continues

#### Scenario: Immutable payload differs
- **WHEN** the core or either fixture differs by any byte from its baseline
- **THEN** the repack fails closed and no runtime qualification is inferred from
  the v0.2.0 evidence

#### Scenario: Runtime requalification decision
- **WHEN** the core and both fixtures remain byte-identical and only release text,
  archive metadata, or checksum files change
- **THEN** another commercial-game run, performance soak, or protocol review is
  not required by this change

### Requirement: Offline release material is tracked and privacy-safe

The repository SHALL track the exact v0.2.0 installation-and-usage guide and
source/provenance notice used in the public archive. The guide MUST agree with
the current public README and operating guidance on installation, network use,
latency policy, saving, limitations, support, privacy, and upstream independence.
Release text MUST NOT disclose private qualification-device nicknames, device
serials, network addresses, private filesystem paths, commercial content,
saves, raw input histories, or unpublished evidence.

#### Scenario: Offline reader uses the bundle
- **WHEN** a reader opens the extracted archive without visiting the repository
- **THEN** the included guide, MPL-2.0 licence, and source/provenance notice
  explain what the alpha is, how to install and connect it, its limits, where
  its exact source is available, and where public support belongs

#### Scenario: Privacy audit finds private material
- **WHEN** a staged standalone or archived text file contains a prohibited
  private identifier or evidence category
- **THEN** packaging fails and identifies the offending file before publication

#### Scenario: Public guidance changes before packaging
- **WHEN** current README, support, security, or operating guidance conflicts
  materially with the tracked offline guide
- **THEN** the discrepancy is resolved explicitly before the guide is packaged

### Requirement: Checksums cover downloads and extracted payloads

The release SHALL provide two unambiguous SHA-256 scopes. The archive's internal
`SHA256SUMS` SHALL cover every archive payload member except itself. The
standalone release `SHA256SUMS` SHALL cover every other downloadable release
asset and SHALL exclude itself. Verification MUST reject missing, extra,
duplicate, renamed, or mismatched archive members and release assets.

#### Scenario: Clean archive verification
- **WHEN** the corrected archive is extracted into an empty directory and its
  internal manifest is checked
- **THEN** every intended member is present exactly once, no undeclared member
  is present, and every member hash passes

#### Scenario: Clean release verification
- **WHEN** every corrected public asset is redownloaded into an empty directory
  and the standalone manifest is checked
- **THEN** every advertised asset other than the manifest is present exactly
  once and passes its recorded hash

#### Scenario: Stale or mixed asset set
- **WHEN** an old guide or archive is paired with the corrected checksum manifest,
  or a corrected asset is paired with the old manifest
- **THEN** verification fails and the release is not declared ready to share

### Requirement: An in-place prerelease correction is transparent and recoverable

The v0.2.0 release SHALL disclose that its guide, archive, and checksums were
repacked for documentation, privacy, licence, and provenance only while its
tag, source commit, core, and fixtures remained unchanged. The original release
body and assets MUST be preserved before remote mutation, and the complete
corrected set MUST pass local verification before replacement begins. A failed
or partial publication SHALL restore the original set or keep the release
unadvertised until one coherent set is available.

#### Scenario: Corrected assets are published
- **WHEN** the locally verified corrected set replaces the existing v0.2.0
  documentation, archive, and checksum assets
- **THEN** the release description records the correction, the preserved
  identities, and the new guide/archive/checksum identities without implying a
  binary rebuild

#### Scenario: Remote replacement fails
- **WHEN** an upload, deletion, metadata update, or redownload verification fails
  during publication
- **THEN** the preserved original release can be restored and users are not
  intentionally directed to a mixed artifact set

### Requirement: Sharing scope remains honest and tiered

After the artifact gate passes, the repository and prerelease MAY be linked
publicly as an experimental Android ARM64 two-player Multi-Pak alpha. Shared
wording MUST retain the trusted local-network assumption, same-effective-ROM
requirement, unsupported-feature list, privacy guidance, upstream-independence
statement, and absence of a support promise. Public availability SHALL neither
solicit nor prohibit feedback. Any future organized feedback, telemetry, or
support programme MUST be proposed and reviewed separately rather than inferred
from the existence of issue templates or a public release.

#### Scenario: Limited alpha sharing begins
- **WHEN** verified release assets are publicly available while later roadmap
  work remains incomplete
- **THEN** anyone may access them without the project claiming stability,
  universal compatibility, active support, or a feedback campaign

#### Scenario: Broad report campaign is considered
- **WHEN** the project considers actively soliciting or systematically processing
  reports from a broad audience
- **THEN** that programme requires its own explicit scope, privacy boundary, and
  maintainer decision rather than becoming an implicit alpha obligation

#### Scenario: Stable or universal claim is proposed
- **WHEN** publication wording suggests general stability, internet-safe hostile
  peer handling, or universal Multi-Pak compatibility
- **THEN** the wording is rejected because those outcomes are not established by
  public availability

### Requirement: Limited alpha sharing is neutral toward feedback

The project SHALL NOT require a separately staged cold-reader exercise before
limited experimental-alpha sharing when the public guide has been reviewed, the
published artifacts have been independently verified, and existing playtesting
has already demonstrated working sessions through the documented path. Sharing
the repository or release MUST NOT be presented as a feedback campaign, support
commitment, or request for reports. Existing issue channels SHALL remain available
for unsolicited use without promising a response, and the project SHALL NOT
forbid feedback that a person independently chooses to provide.

#### Scenario: Verified alpha begins limited sharing
- **WHEN** the public guide and artifacts pass review and independent verification
  and existing playtesting covers the documented connection path
- **THEN** sharing may begin without a contrived reader gate or an invitation to
  join a feedback programme

#### Scenario: Instructions omit a necessary step
- **WHEN** a person independently reports a reproducible omission or ambiguity
  through an existing issue channel
- **THEN** the report may be considered through normal project judgment without a
  service-level promise and without requiring a diagnostic intake system

#### Scenario: Gameplay qualification is proposed
- **WHEN** existing playtesting already demonstrates that the published
  installation and connection path works
- **THEN** this capability does not add another commercial playthrough or
  performance soak merely to authorize sharing

### Requirement: Future alpha releases use immutable automated publication

After the automated release-provenance capability is enabled, every new public
Android alpha release SHALL be generated and published by the trusted tag
workflow from its approved annotated tag. The special v0.2.0 in-place
documentation correction SHALL remain historical and MUST NOT authorize future
asset replacement under an existing tag.

#### Scenario: A future alpha is released
- **WHEN** a maintainer pushes an approved new release tag after the automated
  capability is enabled
- **THEN** the exact validated, reproducible, privacy-safe set is published
  automatically and remains immutable

#### Scenario: A published future alpha needs correction
- **WHEN** documentation, metadata, executable, fixture, or provenance is found to
  be wrong after publication
- **THEN** the correction uses a newly reviewed version and tag rather than
  replacing assets or moving the original tag
