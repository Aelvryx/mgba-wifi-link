## RENAMED Requirements

- FROM: `### Requirement: Onboarding feedback is proportionate to limited alpha sharing`
- TO: `### Requirement: Limited alpha sharing is neutral toward feedback`

## MODIFIED Requirements

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

## ADDED Requirements

### Requirement: Future alpha releases use immutable automated publication

After the automated release-provenance capability is enabled, every new public
Android alpha release SHALL be generated and published from its approved
annotated tag through the read-only intake and protected default-branch release
controller. The special v0.2.0 in-place documentation correction SHALL remain
historical and MUST NOT authorize future asset replacement under an existing
tag.

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
