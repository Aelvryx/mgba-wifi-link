## ADDED Requirements

### Requirement: An approved annotated tag starts the complete release

The project SHALL treat creation and push of a new annotated tag in the
canonical `vMAJOR.MINOR.PATCH` form as the maintainer's release decision. The
tag-triggered intake MUST be read-only and unprivileged. A separate controller
whose workflow definition and release authority are loaded from protected
default-branch code MUST independently validate the annotated tag object, peel
it to one commit reachable from protected `master`, validate the version and
tracked release-notes inputs, and then perform every remaining validation,
build, package, provenance, staging, verification, and publication step without
a human approval pause.

#### Scenario: Approved tag is pushed
- **WHEN** a new canonical annotated tag points to reviewed protected history and
  its version and tracked release inputs agree
- **THEN** the complete automated release workflow starts for the peeled commit
  and requires no later dispatch, asset upload, approval, or Publish click

#### Scenario: Tag admission fails
- **WHEN** the tag is lightweight, malformed, reused, moved, off protected
  history, version-inconsistent, missing reviewed release notes, or already bound
  to conflicting release state
- **THEN** the workflow fails before building a publishable set and no public
  release is created or changed

### Requirement: Candidate tag code cannot grant publication authority

The project MUST treat every workflow and executable file reachable only from
the candidate tag as untrusted until protected default-branch controller code
has independently admitted the tag's peeled commit. The tag intake SHALL have no
release secret, environment, attestation, identity-token, contents-write, or
publication authority. Only protected-controller code MAY request release-write
permissions, and it MUST keep trusted release tooling separate from the admitted
source tree used as build and reviewed-data input.

#### Scenario: Off-history tag rewrites its workflow
- **WHEN** a canonical-looking tag points off protected history and its commit
  replaces or omits the intake checks or adds a privileged publisher
- **THEN** no protected controller admission succeeds and the tag-controlled run
  receives no project release credential or mutation authority

#### Scenario: Protected-history tag reaches the controller
- **WHEN** the read-only intake for an approved tag completes
- **THEN** protected default-branch controller code independently resolves and
  admits the remote tag before checking out or executing the admitted source

### Requirement: Release evidence belongs to the exact tagged source

The protected controller MUST execute the same protected normal, ASan/UBSan, TSan,
complete-suite, fixture/tooling, and Android/binary-boundary gates against the
peeled tag commit. It SHALL record the workflow, run, job, source, and conclusion
identities in provenance, SHALL separately record the protected controller
workflow and commit, and SHALL NOT satisfy a tag using an ambiguous or older
branch run.

#### Scenario: Every exact-source gate passes
- **WHEN** every required protected job completes successfully against the peeled
  tag commit
- **THEN** packaging may consume that exact run's admitted build and provenance
  records each required conclusion

#### Scenario: A required gate is missing or fails
- **WHEN** any required job is absent, cancelled, skipped unexpectedly, timed out,
  inconclusive, or unsuccessful for the peeled commit
- **THEN** release creation stops before public staging

### Requirement: Independent Android builds are byte-reproducible

The workflow SHALL build the Android ARM64/API 21+ libretro core twice in
independent clean jobs using the same pinned runner class, NDK, configuration,
and dependency policy. Both outputs MUST be byte-identical and have the same
SHA-256 before one output is admitted to packaging.

#### Scenario: Clean builds match
- **WHEN** both independent jobs build the tagged source
- **THEN** their core bytes and SHA-256 values match and one identified copy is
  admitted as the canonical executable

#### Scenario: Clean builds differ
- **WHEN** either build fails or the two non-empty outputs differ by any byte
- **THEN** the workflow reports the reproducibility failure and publishes nothing

### Requirement: The executable identity is validated before packaging

The admitted core MUST be a non-empty AArch64 ELF for the documented Android
baseline and SHALL contain the full peeled source commit, expected project
version, canonical `mgba-gba-wifi-link` product identity, and exact
`mgba-gba-link-replicated-v2` compatibility identity. Packaging MUST fail when
any expected identity is missing or conflicting.

#### Scenario: Executable identities agree
- **WHEN** the admitted core is inspected before packaging
- **THEN** its architecture and every required embedded identity agree with the
  canonical release metadata

#### Scenario: Executable identity conflicts
- **WHEN** architecture, commit, version, product, or compatibility identity does
  not match the tagged release
- **THEN** no archive, attestation, draft, or public release is produced from that
  executable

### Requirement: Release packaging is deterministic and canonical

A project-owned tested packager SHALL generate release files from explicit
tracked inputs and canonical metadata. It MUST normalize ordering, timestamps,
ownership, modes, line endings, serialization, and compression, reject symlinks
or unsafe paths, and reproduce byte-identical payload and archive bytes for the
same admitted inputs.

The public project asset set SHALL contain exactly the versioned Android core,
the two CC0 fixtures, rendered installation guide, deterministic Android archive,
`RELEASE-PROVENANCE.json`, and standalone `SHA256SUMS`. The archive SHALL contain
exactly the core, fixtures, guide, source/provenance notice, MPL-2.0 licence,
`BUILD-PROVENANCE.json`, and internal `SHA256SUMS`.

#### Scenario: Identical inputs are packaged twice
- **WHEN** two clean packaging runs consume the same admitted tagged inputs
- **THEN** every corresponding non-attestation file is byte-identical and both
  archive-member and release-asset inventories match the canonical allow-list

#### Scenario: Package membership or metadata is unsafe
- **WHEN** a package contains a missing, extra, duplicate, renamed, symlinked,
  absolute, traversal, incorrectly moded, or non-normalized member
- **THEN** verification fails before the package can reach the publisher

### Requirement: Checksum and provenance construction is acyclic

The packager MUST generate provenance and checksum data in a non-recursive order.
`BUILD-PROVENANCE.json` SHALL describe source, toolchain, evidence, configuration,
and sibling archive payload identities without hashing its enclosing archive.
The archive's internal `SHA256SUMS` SHALL cover every other archive member.
`RELEASE-PROVENANCE.json` SHALL record the five non-provenance public payload
assets after the archive exists. The standalone `SHA256SUMS` SHALL be generated
last and cover all six other project assets while excluding itself.

#### Scenario: Manifests are verified from clean directories
- **WHEN** the archive and public asset set are independently copied or downloaded
  into empty directories
- **THEN** each inventory is complete and exclusive and every declared SHA-256
  passes in its stated scope

#### Scenario: A manifest creates a cycle or ambiguous scope
- **WHEN** provenance attempts to hash its enclosing archive or itself, a checksum
  hashes itself, or a member has no unambiguous manifest owner
- **THEN** packaging fails rather than emitting a self-referential or incomplete
  release

### Requirement: Provenance has one versioned machine-readable model

Build and release provenance SHALL use explicit schema versions and canonical
serialization. Together they MUST record repository, annotated tag object,
peeled source commit, protected controller workflow/commit, first-run workflow/
run/job identities, resolved runner image, pinned action and toolchain identities,
build configuration, deterministic epoch, required gate conclusions, and the
applicable file names, sizes, and SHA-256 values. The human source notice and
release body SHALL derive identity fields from that model.

#### Scenario: Provenance consumers compare release surfaces
- **WHEN** a verifier compares the machine manifests, human notice, release body,
  archive, executable, tag, and workflow evidence
- **THEN** every shared identity agrees and the signed versus reproducible
  provenance boundary is explicit

#### Scenario: Provenance is missing or inconsistent
- **WHEN** a required field, schema, evidence link, toolchain identity, file digest,
  or rendered identity is absent, malformed, or conflicting
- **THEN** the release workflow fails before publication

### Requirement: GitHub attestations do not alter reproducible bytes

The privileged release job SHALL issue GitHub build-provenance attestations for
at least the admitted core and deterministic archive using the exact verified
subjects. Attestation envelopes MUST remain outside archive and standalone
checksum scopes and MUST NOT modify the subject bytes.

#### Scenario: Attestations are issued
- **WHEN** canonical core and archive subjects have passed every local gate
- **THEN** their signed attestations bind the recorded repository, workflow, and
  subject digests without changing either file

#### Scenario: Attestation fails
- **WHEN** the platform cannot issue or verify a required attestation for the
  exact subject
- **THEN** automatic publication stops and no unsigned substitute is silently
  accepted

### Requirement: Automated publication preserves the privacy boundary

Release construction SHALL use explicit file and field allow-lists and MUST fail
on raw or hashed commercial ROM/BIOS identities, saves, input histories, endpoint
or frontend logs, private paths, device serials or nicknames, network addresses,
unpublished qualification evidence, secrets, or unexpected files. Synthetic
privacy canaries SHALL exercise every prohibited category.

#### Scenario: Allowed public metadata is packaged
- **WHEN** inputs contain only reviewed release text, public repository/build
  identities, redistributable fixtures, executable data, and canonical manifests
- **THEN** privacy validation succeeds without adding user-feedback or telemetry
  data

#### Scenario: Prohibited material reaches staging
- **WHEN** any file, field, generated text, archive member, release body, or
  workflow artifact contains a prohibited canary or unapproved category
- **THEN** the workflow identifies the bounded offending category and publishes
  nothing

### Requirement: Build and publication privileges are separated

The tag intake and every validation, test, build, reproducibility, and packaging
job SHALL run with read-only repository permissions and without release
credentials. Only a final publisher defined by protected default-branch
controller code MAY receive the minimum `contents`, `attestations`, and
`id-token` write permissions, and that job MUST consume and re-verify the
canonical workflow artifact without checking out source or rebuilding assets.

#### Scenario: Canonical artifacts reach the publisher
- **WHEN** all unprivileged jobs succeed and transfer the canonical release set
- **THEN** the publisher verifies its manifest and digests before its first GitHub
  release or attestation mutation

#### Scenario: A privileged job can rebuild or substitute an asset
- **WHEN** workflow policy inspection finds source checkout, compilation, package
  rendering, undeclared downloads, or asset substitution in the publisher
- **THEN** the release workflow fails its policy test and is not eligible to run

### Requirement: Public release creation is transactional and automatic

After all gates pass, the publisher SHALL create a private draft for the exact
tag, upload exactly the canonical seven assets, read them back or query their
verified content, and compare names, count, sizes, hashes, body identity, tag,
target commit, and prerelease classification. It MUST publish that draft
automatically in the same workflow only after every comparison succeeds.

#### Scenario: Remote draft matches completely
- **WHEN** the uploaded draft and all seven assets match the canonical local set
- **THEN** the workflow publishes the release automatically without a human pause
  and verifies the resulting public state

#### Scenario: Upload or draft verification fails
- **WHEN** draft creation, upload, metadata rendering, remote read-back, or any
  comparison fails before publication
- **THEN** automation removes its non-public draft when safe, retains workflow
  evidence, and exposes no partial public release

#### Scenario: Publication response is uncertain
- **WHEN** the publish request returns an ambiguous transport or response failure
- **THEN** automation reads the release back and succeeds only for the exact
  complete public state; a conflicting state is reported and never automatically
  deleted or overwritten

### Requirement: Release retries are idempotent and immutable

The release controller SHALL serialize runs per tag without cancellation. Before
starting protected gates or builds, it MUST inspect any existing release. A rerun
after successful publication MUST download and validate the original seven
assets, exclusive checksum scopes, package/provenance schemas, source/tag/
controller identities, release body, classification and attestations as one
coherent first-run record. That exact record SHALL be read-only success even
though the rerun has different workflow/job identities. Any conflict MUST fail.
Published assets, metadata, tags, and target commits SHALL NOT be replaced in
place; corrections use a new version and tag.

#### Scenario: Exact release workflow is rerun
- **WHEN** the immutable tag already has a complete public release matching every
  canonical identity and asset
- **THEN** the rerun verifies the retained first-run evidence before rebuilding
  and exits successfully without upload, deletion, metadata mutation, or use of
  the rerun's new job identities in regenerated package bytes

#### Scenario: Existing release conflicts
- **WHEN** the tag has a draft or public release with a different target, body,
  classification, inventory, size, or digest
- **THEN** the workflow fails loudly and preserves the existing public state for
  investigation

### Requirement: Release tags are protected against movement and deletion

The repository MUST apply and verify a ruleset covering canonical `v*` release
tags that permits deliberate creation but rejects force-update and deletion. The
workflow SHALL record the tag object and peeled commit and MUST fail if the remote
tag does not still resolve to both identities immediately before publication.

#### Scenario: Tag remains immutable
- **WHEN** a release proceeds from validation to publication
- **THEN** the remote tag object and peeled commit remain unchanged and are
  recorded in provenance

#### Scenario: Tag changes or disappears
- **WHEN** the release tag is moved, replaced, deleted, or resolves differently at
  the final pre-publication check
- **THEN** publication aborts and any private draft is cleaned up safely

### Requirement: GitHub operations use supported bounded interfaces

The production GitHub adapter MUST construct command-specific invocations for
REST, release, and attestation operations. Repository REST endpoints SHALL bind
the repository in the endpoint path and MUST NOT receive unsupported `gh api`
options. Binary responses MUST be streamed to an explicitly opened safe regular
destination. Tests SHALL exercise a faithful CLI parser boundary, and a read-only
live GET smoke MUST pass before external mutation is enabled.

#### Scenario: Real read-only REST operation is parsed
- **WHEN** the adapter requests canonical release or ruleset metadata through the
  installed supported GitHub CLI
- **THEN** the command uses only supported options, returns bounded validated
  data, and performs no mutation

#### Scenario: Unsupported or unsafe command shape is introduced
- **WHEN** an API invocation gains an unsupported repository/output option,
  shell interpolation, an unbounded response, or an unsafe download destination
- **THEN** faithful adapter tests reject the change before rehearsal or release

### Requirement: Release notes are reviewed tracked inputs

Every release tag MUST have an exact-version tracked release-notes input with no
unresolved placeholder. Automation SHALL render factual source, build, checksum,
and provenance fields from canonical data but MUST NOT infer compatibility,
qualification, support, or behavioral claims from commit messages.

#### Scenario: Reviewed notes match the tag
- **WHEN** the exact-version notes exist and pass placeholder, privacy, identity,
  and scope validation
- **THEN** the release body combines those reviewed claims with generated factual
  provenance

#### Scenario: Notes are missing or overclaim
- **WHEN** the notes are absent, version-inconsistent, unresolved, privacy-unsafe,
  or conflict with current documented scope
- **THEN** tag admission fails and no generated release body substitutes invented
  claims

### Requirement: Runtime qualification remains proportionate and precedes tagging

The release automation SHALL NOT create a new physical-gameplay requirement for
tooling-only changes. When the tagged range changes runtime-sensitive behavior,
the applicable reviewed qualification evidence MUST already be complete before
the maintainer creates the approved tag, and provenance SHALL record the decision
or evidence reference without embedding private raw evidence.

#### Scenario: Release range is tooling-only
- **WHEN** review proves no runtime, protocol, scheduling, persistence, input,
  presentation, audio, or teardown behavior changed
- **THEN** protected automated evidence is sufficient and the release workflow
  does not manufacture a device playtest

#### Scenario: Release range changes runtime behavior
- **WHEN** the release includes behavior governed by a physical qualification gate
- **THEN** the tag is not approved until that gate is complete and the release
  records its bounded public decision evidence

### Requirement: Publication behavior is rehearsed without risking a real release

Before enabling the production `v*` intake/controller pair, the project MUST
exercise tag admission, protected-controller ownership, exact-source validation,
packaging, privilege separation, draft staging, remote verification, automatic
publication, original-evidence rerun, failure cleanup, and deletion through one
exact disposable public repository containing synthetic/re-distributable inputs
only. A reviewed rehearsal generator MUST change only an allow-listed set of
canonical repository, signer, policy and synthetic-note identities for the exact
disposable name; production tooling SHALL retain no general repository override.
The project MUST preflight attestation availability and repository deletion
authority before creating the target and MUST document that public attestation
transparency entries may outlive repository deletion. The rehearsal SHALL use no
production release tag or commercial content.

#### Scenario: Isolated rehearsal succeeds
- **WHEN** the complete workflow is run against the approved disposable target
- **THEN** every intended state transition and remote verification is recorded and
  the temporary published result, tag, and exact repository are removed after
  inspection while the bounded public transparency consequence is recorded

#### Scenario: Rehearsal fault is injected
- **WHEN** upload, checksum, metadata, publish-response, retry, or cleanup failure
  is simulated
- **THEN** the workflow demonstrates the same fail-closed and idempotent semantics
  required for production without altering a real release

#### Scenario: Rehearsal identity transformation escapes its allow-list
- **WHEN** the generated rehearsal tree changes a runtime file, production
  identity outside the exact disposable target, package contract, or undeclared
  path
- **THEN** rehearsal generation fails before repository creation or push and the
  canonical production tree remains unchanged
