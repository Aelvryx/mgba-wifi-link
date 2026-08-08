## 1. Freeze the release contract and historical baseline

- [x] 1.1 Record the immutable v0.2.0 tag, commit, release metadata, asset hashes,
  archive members, and checksum scopes as a regression fixture.
- [x] 1.2 Inventory the protected CI jobs, Android build command, toolchain inputs,
  fixture checks, and binary-boundary checks reused by releases.
- [x] 1.3 Freeze canonical semantic-version tags, prerelease classification,
  public-asset/archive inventories, modes, and normalized ZIP metadata.
- [x] 1.4 Define version-neutral guide/provenance templates and exact-version
  reviewed release notes without changing historical v0.2.0 files.
- [x] 1.5 Add synthetic redistributable release inputs and privacy canaries.

## 2. Implement trusted tag admission and metadata

- [x] 2.1 Test annotated-tag parsing, peeling, canonical version extraction,
  `master` reachability, notes lookup, and prerelease classification.
- [x] 2.2 Test lightweight, malformed, moved, missing, off-history,
  version-conflicting, placeholder-containing, and conflicting release inputs.
- [x] 2.3 Implement canonical tag/source metadata and bounded machine-readable
  admission failures without GitHub mutation.
- [x] 2.4 Capture tag-object and peeled-commit identities independently and recheck
  them before publication.
- [x] 2.5 Prevent reviewed notes from overriding generated source, artifact,
  checksum, provenance, workflow, or compatibility identities.
- [ ] 2.6 Remove controller/intake correlation fields and tests; retain the actual
  trusted tag `push` event and exact tagged-source evidence only.

## 3. Build deterministic artifacts and provenance

- [x] 3.1 Test exact public/archive inventories, canonical names, modes, ordering,
  timestamps, line endings, JSON bytes, and compression settings.
- [x] 3.2 Reject missing, extra, duplicate, renamed, symlinked, absolute,
  traversal, unsafe-mode, and non-normalized inputs.
- [x] 3.3 Render deterministic guide and source/provenance documents.
- [x] 3.4 Generate canonical build provenance from source, gate, toolchain,
  configuration, build, and sibling-payload identities.
- [x] 3.5 Generate the archive and its non-recursive internal `SHA256SUMS`.
- [x] 3.6 Generate release provenance and standalone `SHA256SUMS` in acyclic order.
- [x] 3.7 Verify clean extracted/archive and standalone scopes and prove two
  synthetic builds produce byte-identical output.
- [ ] 3.8 Remove protected-controller and signed-attestation identities from the
  provenance contract, goldens, renderers, and retained-release parser.

## 4. Preserve privacy and bounded parsing

- [x] 4.1 Enforce explicit public file/field allow-lists and synthetic canaries for
  private paths, addresses, ROM/BIOS data, saves, inputs, logs, device identities,
  commercial evidence, tokens, and secrets.
- [x] 4.2 Validate safe regular files, paths, modes, exclusive membership, and
  bounded category-only diagnostics.
- [x] 4.3 Add contract-owned public-asset, JSON, ZIP member, aggregate, and
  decompression limits with streaming download ceilings and partial cleanup.
- [x] 4.4 Extend the permanent product-boundary audit for release tooling without
  changing runtime product identities.

## 5. Keep publication automatic, transactional, and retry-safe

- [x] 5.1 Define a mockable GitHub boundary for release inspection, private draft
  creation/deletion, upload, download, publication, and remote verification.
- [x] 5.2 Test successful draft/upload/read-back/publish and every safe failure,
  cleanup, conflict, and ambiguous-response path.
- [x] 5.3 Use command-specific supported `gh api`/`gh release` invocations, bounded
  JSON, safe streaming downloads, and a parser-faithful fake plus live GET smoke.
- [x] 5.4 Implement early existing-public-release verification from retained
  first-run assets, manifests, provenance, body, target, and classification.
- [x] 5.5 Test two attempts with different run/job IDs: exact retained state is
  read-only success and every conflict performs zero mutation.
- [ ] 5.6 Remove mandatory GitHub attestation creation/verification from the
  client, publisher, retained-release path, tests, and workflow.
- [ ] 5.7 Re-run publisher and rerun tests against the simplified provenance-only
  transaction and preserve no-replace semantics.

## 6. Compose one trusted tag workflow

- [ ] 6.1 Simplify `.github/workflows/gba-wifi-link-release.yml` to the trusted
  annotated-tag model with no manual gate and no separate controller.
- [ ] 6.2 Keep the existing six protected exact-source gates and two independent
  Android builds; require matching bytes and embedded identities.
- [ ] 6.3 Run deterministic packaging twice, compare complete output, seal one
  exact handoff, and verify it before publication.
- [ ] 6.4 Give read-only jobs read permissions and only the final publisher
  `contents: write`; remove `id-token` and `attestations` permissions.
- [ ] 6.5 Remove the governance workflow, tracked tag ruleset, ruleset fixtures,
  audit credential/environment instructions, and tag-policy module/tests.
- [ ] 6.6 Replace adversarial controller policy tests with concise checks for the
  tag trigger, exact-source gates, dual builds, handoff, publisher isolation,
  automatic publication, and absence of manual dispatch/approval.
- [ ] 6.7 Ensure exact existing public releases exit before protected builds and
  drafts/conflicts fail without public mutation.

## 7. Align guidance with the proportionate workflow

- [ ] 7.1 Update release documentation: prepare notes, create/push one annotated
  tag, and let automation finish; document failure/rerun/new-version correction.
- [ ] 7.2 Remove protected-controller, hostile-tag, governance-secret, ruleset,
  rehearsal-repository, and signed-attestation language from current guidance.
- [x] 7.3 Preserve neutral feedback language, passive issue forms, privacy
  guidance, upstream independence, and the maintainable-alpha roadmap direction.
- [ ] 7.4 Update issue #21 and its milestone to describe the final trusted-tag
  automation after the implementation is green.

## 8. Verify and land

- [ ] 8.1 Run all release admission, package, privacy, provenance, publisher,
  rerun, workflow-policy, and boundary tests locally.
- [ ] 8.2 Build the synthetic release twice in clean directories and compare every
  byte; verify both archive and standalone scopes.
- [ ] 8.3 Run Python/shell/YAML/JSON syntax checks and strict OpenSpec validation.
- [ ] 8.4 Run focused normal, ASan/UBSan, TSan, complete suite, fixture/analyzer/
  helper checks, and Android ARM64 build on the exact draft-PR head.
- [x] 8.5 Confirm no GBA runtime, protocol, input, RTC, persistence, scheduling,
  audio, video, or teardown behavior changed; no physical replay is required.
- [ ] 8.6 Perform one inline self-review against the approved trusted-maintainer
  threat model; findings outside that model require user/spec approval.
- [ ] 8.7 Produce `verify.md`, write the retrospective including this scope
  correction, sync/archive the specs, and push the final commits to PR #31.
- [ ] 8.8 Mark PR #31 ready and merge only after the exact final head is green.

No new production tag or release is created by this change. The first future
approved tag is the first real use of the automation.
