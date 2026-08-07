## ADDED Requirements

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

### Requirement: Newcomer onboarding is tested proportionately

Before the corrected alpha is deliberately shared, one person who did not
author the release instructions SHALL use only the public release material to
install the exact core and establish a short two-device link session. The
interactive portion SHALL be time-boxed to twenty minutes after downloads are
available, SHALL prefer redistributable fixtures, and SHALL test onboarding
rather than sustained gameplay or performance. Automation SHALL own artifact
identity and evidence checks; the human SHALL own frontend navigation and
ordinary interactive confirmation.

#### Scenario: Cold reader succeeds unaided
- **WHEN** the reader follows the public guide without undocumented maintainer
  instructions and reaches one ready linked session within the time box
- **THEN** the onboarding gate passes and only a brief sanitized outcome and any
  genuine friction are retained

#### Scenario: Instructions omit a necessary step
- **WHEN** the reader cannot proceed without maintainer knowledge that is absent
  from the public material
- **THEN** the gate records a documentation defect, the guide is corrected, and
  only the failed onboarding portion is repeated

#### Scenario: Gameplay qualification is proposed
- **WHEN** the short linked session demonstrates that the published installation
  and connection path works
- **THEN** this change does not extend the rehearsal into a commercial playthrough
  or performance soak

### Requirement: Sharing scope remains honest and tiered

After artifact and cold-reader gates pass, the repository MAY be linked to
friends, enthusiasts, and technically comfortable testers as an experimental
Android ARM64 two-player Multi-Pak alpha. Shared wording MUST retain the trusted
local-network assumption, same-effective-ROM requirement, unsupported-feature
list, privacy guidance, and upstream-independence statement. A broad campaign
that actively solicits support reports from strangers SHALL wait for the
sanitized diagnostic-bundle outcome tracked separately in issue #20.

#### Scenario: Limited alpha sharing begins
- **WHEN** the corrected assets and newcomer rehearsal pass while v0.2.1 issues
  #20 through #23 remain incomplete
- **THEN** limited public-alpha sharing may begin without claiming the later
  supportable-alpha milestone

#### Scenario: Broad report campaign is considered
- **WHEN** the project plans to solicit troubleshooting reports from a broad
  unfamiliar audience
- **THEN** it first provides the separately reviewed sanitized diagnostic-bundle
  workflow or explicitly narrows the campaign back to limited alpha sharing

#### Scenario: Stable or universal claim is proposed
- **WHEN** publication wording suggests general stability, internet-safe hostile
  peer handling, or universal Multi-Pak compatibility
- **THEN** the wording is rejected because those outcomes are not established by
  this change
