## 1. Freeze the release contract and historical baseline

- [x] 1.1 Record the current v0.2.0 tag, peeled commit, six public asset names,
  asset hashes, archive membership, internal hashes, release metadata, and
  immutable core/fixture identities as a historical regression fixture.
- [x] 1.2 Inventory the protected CI jobs, Android build command, runner image,
  NDK/toolchain downloads, action versions, binary-boundary checks, fixture build,
  and current packaging inputs that the release workflow must reuse.
- [x] 1.3 Freeze schema-v1 field names, types, ordering, required/optional policy,
  canonical JSON serialization, and privacy allow-list for build and release
  provenance before generating golden fixtures.
- [x] 1.4 Freeze canonical semantic-version tag syntax, annotated-tag requirements,
  version-to-prerelease mapping, exact seven public asset names, exact archive
  members, modes, checksum scopes, and normalized ZIP parameters.
- [x] 1.5 Define version-neutral tracked guide/source templates and the exact-version
  reviewed release-notes path without modifying the historical v0.2.0 directory.
- [x] 1.6 Add synthetic public release inputs containing a fake core, fake fixtures,
  release notes, source identities, gate results, runner/toolchain metadata, and
  privacy canaries; do not use commercial or private qualification data.

## 2. Implement tag admission and canonical metadata test-first

- [x] 2.1 Add failing tests for valid annotated tag parsing, peeling, canonical
  version extraction, protected-`master` reachability, exact notes lookup, and
  prerelease classification.
- [x] 2.2 Add failing tests for lightweight, malformed, moved, reused, missing,
  off-history, version-conflicting, placeholder-containing, and already-conflicting
  tag/release inputs.
- [x] 2.3 Implement the minimum tag-admission and canonical-metadata module needed
  to pass the valid and invalid vector tests.
- [x] 2.4 Add tests that the remote tag object and peeled commit are captured
  independently and must remain stable through the final publication check.
- [x] 2.5 Add tests that release notes cannot supply or override generated source,
  artifact, compatibility, checksum, provenance, or workflow identities.
- [x] 2.6 Add tests rejecting missing release notes, unresolved placeholders,
  mismatched version claims, prohibited privacy fields, and unsupported stable
  classification for `v0.x`.
- [x] 2.7 Implement tracked release-note validation and deterministic release-body
  rendering from reviewed prose plus canonical generated facts.
- [x] 2.8 Expose a non-publishing CLI entry point that emits canonical admitted
  metadata or a bounded machine-readable failure without mutating GitHub.
- [ ] 2.9 Extend admission metadata to distinguish the actual tag `push` intake,
  protected controller workflow/commit, and released source commit; reject any
  ambiguous workflow-run/tag correlation before candidate execution.

## 3. Implement deterministic package construction test-first

- [x] 3.1 Add failing golden tests for exact public-asset and archive-member
  inventories, canonical names, file modes, ordering, timestamps, ownership,
  line endings, JSON bytes, and compression settings.
- [x] 3.2 Add failing tests for missing, extra, duplicate, renamed, symlinked,
  absolute, traversal, non-regular, unsafe-mode, and non-normalized inputs.
- [x] 3.3 Implement deterministic guide and human provenance rendering from the
  canonical model without copying runtime-private or historical correction data.
- [x] 3.4 Implement canonical `BUILD-PROVENANCE.json` construction and validate its
  schema, exact protected evidence, source, runner, action, toolchain, configuration,
  deterministic epoch, and sibling-payload identities.
- [x] 3.5 Implement deterministic archive construction and its internal
  `SHA256SUMS`, covering every archive member except the checksum file itself.
- [x] 3.6 Implement canonical `RELEASE-PROVENANCE.json` after archive construction,
  recording the five non-provenance public payload assets without recursive hashes.
- [x] 3.7 Implement standalone `SHA256SUMS` last, covering all six preceding project
  assets and excluding itself and GitHub-generated source archives/attestations.
- [x] 3.8 Implement clean-directory verification for both extracted archive and
  standalone release scopes, including exclusive membership and every SHA-256.
- [x] 3.9 Add a two-run golden test proving that identical synthetic inputs produce
  byte-identical rendered documents, provenance, checksums, and archive bytes.
- [x] 3.10 Add a regression proving that changing any declared input changes the
  intended dependent output while leaving unrelated immutable payloads untouched.
- [ ] 3.11 Add protected-controller identity to canonical provenance and golden
  fixtures while retaining first-run run/job identities as immutable published
  evidence rather than reproducible rerun inputs.

## 4. Enforce release privacy and provenance boundaries

- [x] 4.1 Add field-level allow-list tests for repository, tag, source, public
  workflow, runner/toolchain, build configuration, asset, and qualification-decision
  metadata.
- [x] 4.2 Add file-level allow-list tests for every public asset and archive member
  and fail on any undeclared staging file or directory.
- [x] 4.3 Add synthetic canary tests for ROM/BIOS identities, saves, raw inputs,
  endpoint/frontend logs, paths, addresses, serials, device nicknames, commercial
  evidence, tokens, secrets, and unexpected hashes.
- [x] 4.4 Implement fail-closed privacy validation over source templates, rendered
  text, canonical JSON, archive members, release body, and workflow artifact
  inventory with bounded category-only diagnostics.
- [x] 4.5 Add tests that provenance contains no recursive digest, no environment
  secret, and no signed attestation envelope, while human and machine identity
  fields remain mutually consistent.
- [x] 4.6 Extend the permanent product-boundary audit to recognize the new release
  tooling/workflow and prohibit retired product identities or versioned runtime
  leakage in user-facing release names.

## 5. Implement the transactional publisher behind a mockable boundary

- [x] 5.1 Define a narrow publisher interface for inspecting releases, creating and
  deleting private drafts, uploading assets, reading remote metadata/assets,
  publishing, and verifying attestations; keep package construction outside it.
- [x] 5.2 Add a fake GitHub CLI/API fixture that records commands and simulates exact
  responses without network access or repository mutation.
- [x] 5.3 Add failing publisher tests for the successful sequence: local verify,
  no-conflict check, private draft, seven uploads, remote read-back, attestation,
  automatic publish, and final public verification.
- [x] 5.4 Add failing tests for draft creation, individual upload, count, size, hash,
  body, tag, target, classification, attestation, and pre-publish read-back failures.
- [x] 5.5 Add tests that every safe pre-publication failure removes only the
  publisher-owned private draft and never deletes or edits public state.
- [x] 5.6 Add tests for an ambiguous publish response followed by exact public
  read-back success and by conflicting public read-back failure.
- [x] 5.7 Add tests for a byte-identical published-release rerun that performs no
  mutation, and for every conflicting existing draft/public-release condition.
- [x] 5.8 Implement the minimum publisher orchestration needed to pass the fake
  boundary tests, with stable exit reasons and no implicit asset replacement.
- [x] 5.9 Add a policy test proving the publisher input is the verified workflow
  artifact and that the privileged path cannot check out source, compile, render,
  download undeclared inputs, or substitute files.
- [x] 5.10 Replace the generic GitHub subprocess builder with command-specific
  `gh api`, `gh release`, and `gh attestation` invocations; add faithful parser
  fixtures and a read-only live GET smoke that reject unsupported flags.
- [x] 5.11 Add an early existing-public-release verifier that downloads the
  original seven assets and validates inventory, manifests, provenance, body,
  tag/target/classification, and exact attestations without rebuilding.
- [x] 5.12 Add a two-attempt regression with different workflow/run/job IDs proving
  an exact published first attempt makes the second attempt read-only success,
  while any retained-evidence conflict fails without mutation.

## 6. Compose protected CI and the trusted release controller

- [x] 6.1 Pin every third-party action used by the privileged or artifact-producing
  release path to a reviewed full commit SHA and record the human-readable action
  version in provenance.
- [x] 6.2 Refactor the protected CI workflow into a reusable form without changing
  its pull-request or protected-`master` triggers, job definitions, sanitizer
  settings, fixture behavior, target counts, or pinned upstream exception.
- [x] 6.3 Add regression/static checks proving the six protected jobs still run for
  pull requests and `master`, and the reusable invocation checks out and tests the
  caller's exact peeled tag commit.
- [x] 6.4 Make the Android protected job upload its inspected core and canonical
  build metadata through a fixed artifact contract without granting write
  permissions.
- [x] 6.5 Add a second independent clean Android build job with the same pinned
  NDK/configuration and compare both core bytes and hashes before admission.
- [ ] 6.6 Split release execution into a read-only tag-intake workflow and a
  protected-default-branch `workflow_run` controller that independently resolves
  and admits the remote annotated tag before candidate checkout or execution.
- [ ] 6.7 Separate trusted controller tooling from admitted source/build data and
  place the final publisher only in controller-owned code with the required
  `contents: write`, `attestations: write`, and `id-token: write` permissions.
- [ ] 6.8 Issue and verify GitHub build-provenance attestations for the exact
  admitted core and deterministic archive while keeping envelopes outside package
  checksum scopes and binding both released source and protected controller
  workflow identities.
- [ ] 6.9 Add workflow-policy tests for intake/controller triggers, default-branch
  authority, concurrency, permissions, job dependencies, immutable handoff, pinned
  actions, publisher isolation, and absence of a manual publication gate.
- [ ] 6.10 Add adversarial failure fixtures proving an off-history tag can rewrite
  its intake yet cannot obtain release authority, and that missing gates,
  mismatched builds, failed packaging/attestation, tag movement, or remote conflict
  cannot reach publication.

## 7. Establish immutable tag policy and current project guidance

- [x] 7.1 Define the exact GitHub ruleset JSON for canonical `v*` tags, allowing
  deliberate creation while denying force-update and deletion with the smallest
  necessary bypass set.
- [x] 7.2 Add a read-only validator and mocked tests for ruleset name, repository,
  target pattern, enforcement state, update/deletion restrictions, bypass actors,
  and unexpected policy drift.
- [x] 7.3 Update maintainer instructions so preparing reviewed notes and pushing one
  annotated approved tag is the entire release action; document automatic failure,
  rerun, immutable-correction, and rollback behavior.
- [x] 7.4 Add version-neutral installation/provenance templates and a synthetic
  example release-notes input; keep `packaging/gba-wifi-link/v0.2.0` unchanged and
  explicitly historical.
- [x] 7.5 Update README and public packaging language to describe fully automated
  immutable future releases without promising stable status or upstream support.
- [x] 7.6 Update ROADMAP.md from “supportable alpha” to the approved “maintainable
  alpha,” remove issue #20 from the release gate, place issue #21 first, and keep
  the project neutral rather than soliciting or forbidding feedback.
- [x] 7.7 Update SUPPORT.md and issue-template guidance only as needed to retain
  available issue channels without a report request, service promise, or diagnostic
  bundle dependency.
- [ ] 7.8 Update issue #21 and the v0.2.1 milestone description/title to match full
  tag-triggered automation; remove/defer issue #20 from that milestone without
  closing issue channels or presenting feedback as forbidden.
- [ ] 7.9 Obtain focused review of the proposed tag ruleset and publisher permission
  boundary before any repository-setting or remote rehearsal mutation.
- [ ] 7.10 Apply the reviewed `v*` tag ruleset through the GitHub API, read it back,
  and prove it exactly matches the tracked policy fixture before enabling the
  production release trigger.

## 8. Run local, sanitizer, and protected validation

- [ ] 8.1 Run tag-admission, metadata, packager, privacy, provenance, publisher,
  workflow-policy, tag-ruleset, and boundary unit/golden tests locally.
- [ ] 8.2 Run deterministic packaging twice in separate clean temporary directories
  and compare every synthetic release byte, manifest, archive member, and rendered
  identity.
- [ ] 8.3 Run Python/shell syntax checks and applicable linters for every new tool,
  workflow, template, policy fixture, and test.
- [ ] 8.4 Run strict OpenSpec validation and verify the delta requirements, scenarios,
  task ledger, and implementation plan remain coherent with the implemented
  fully-automated boundary.
- [ ] 8.5 Run the focused normal, ASan/UBSan with leak detection, and TSan suites.
- [ ] 8.6 Run the complete applicable mGBA suite and separately confirm any unchanged
  pinned upstream exception.
- [ ] 8.7 Rebuild the redistributable fixtures byte-identically and run structured
  analyzers, qualification-helper tests, and the permanent product-boundary audit.
- [ ] 8.8 Build the Android ARM64 core twice from the exact review head and prove
  byte identity, ELF/embedded identities, toolchain provenance, and package
  reproducibility.
- [x] 8.9 Confirm through source and normalized diffs that no GBA runtime, protocol,
  input, RTC, persistence, scheduling, audio, video, or teardown behavior changed;
  require physical replay only if that proof fails.
- [x] 8.10 Create one reviewable implementation commit, push it, and open the single
  working draft PR required for protected CI and external rehearsal; keep that PR
  draft through verification, retrospective, and archive.
- [ ] 8.11 Require all protected jobs to pass on the exact draft-PR head and record
  their run/job identities for independent review.

## 9. Rehearse the complete remote transaction safely

- [ ] 9.1 Before remote creation, verify public-repository attestation support and
  exact repository-deletion authority, then derive one bounded decimal run ID and
  exact disposable public repository name.
- [ ] 9.2 Add a reviewed rehearsal generator that copies the release tree and changes
  only allow-listed canonical repository/signer/policy identities for that exact
  disposable name, installs tracked synthetic notes, and proves production retains
  no general repository override.
- [ ] 9.3 Provision the exact empty public disposable repository with explicit
  protected `master`, apply/read back its immutable tag ruleset, and refuse any
  owner/name/visibility/default-branch mismatch before pushing content.
- [ ] 9.4 Run the complete read-only-intake/protected-controller flow with only
  synthetic/re-distributable inputs, including exact-source gates, dual builds,
  deterministic packaging, attestations, private staging, read-back, and automatic
  publication.
- [ ] 9.5 Independently download the rehearsal release into a clean directory and
  verify its seven-asset inventory, standalone manifest, archive membership,
  internal manifest, machine provenance, attestations, tag object, peeled commit,
  prerelease state, and release body.
- [ ] 9.6 Rerun the same immutable tag with new workflow/job IDs and prove the
  original public evidence is verified before rebuild and no asset, metadata, tag,
  release, attestation, or body mutation occurs.
- [ ] 9.7 Exercise isolated partial-upload, remote hash/body conflict, ambiguous
  publish response, tag movement/deletion attempt, and safe private-draft cleanup
  paths without altering a production tag or release.
- [ ] 9.8 Record bounded public-safe rehearsal evidence and confirm no endpoint log,
  commercial content, private path/address, device identity, save, input, or secret
  entered the repository, workflow artifact, attestation, or release; disclose that
  synthetic public attestation transparency entries may remain after deletion.
- [ ] 9.9 Delete the disposable rehearsal release, tag, and repository using
  exact validated identifiers; verify they are absent and record that the cleanup
  was destructive and unrecoverable.
- [ ] 9.10 Obtain independent implementation review covering packager bytes,
  provenance/checksum DAG, privacy, workflow privilege separation, tag policy,
  remote transaction semantics, rerun behavior, and exact protected evidence.
- [ ] 9.11 Address every Critical or Important review finding, rerun affected tests,
  and repeat only the rehearsal portions whose behavior or evidence changed.

## 10. Verify, archive, and land without publishing a production release

- [ ] 10.1 Complete `verify.md` against every requirement and task, including exact
  local/protected/rehearsal evidence and the decision that no physical gameplay is
  required when runtime behavior is unchanged.
- [ ] 10.2 Complete `retrospective.md`, recording workflow friction, reproducibility
  results, external-action safety, review findings, remaining limits, and any
  follow-up that does not block issue #21.
- [ ] 10.3 Sync the `automated-release-provenance` and modified
  `public-alpha-distribution` capability specs to `openspec/specs`.
- [ ] 10.4 Archive `automate-release-artifacts-provenance`, update any boundary-audit
  path classification, and rerun strict OpenSpec and protected validation on the
  immutable archive head.
- [ ] 10.5 Confirm the production `v*` trigger and reviewed tag ruleset are enabled,
  but do not create a real version tag or publish v0.2.1 as part of this change.
- [ ] 10.6 Make the existing PR ready only after verification, retrospective,
  archive, exact-head checks, and independent review are complete; then merge it
  through protected `master`.
- [ ] 10.7 Verify the merged `master`, workflow definitions, repository ruleset,
  authoritative specs, and absence of any rehearsal/production release residue.
- [ ] 10.8 Close issue #21 with exact merged evidence and leave future release
  publication to the next deliberately pushed approved annotated tag.
