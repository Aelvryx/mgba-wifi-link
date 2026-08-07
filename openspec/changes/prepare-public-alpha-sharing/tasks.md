## 1. Preserve the published baseline

- [x] 1.1 Download the current v0.2.0 release body, asset metadata, core,
  fixtures, guide, archive, and checksum manifest into a private temporary
  rollback directory.
- [x] 1.2 Verify the downloaded assets against the original standalone
  `SHA256SUMS` and verify the archive's internal manifest before using any
  payload as a repack input.
- [x] 1.3 Assert and record the immutable tag, source commit, Android-core hash,
  and two fixture hashes from the approved design.
- [x] 1.4 Record the original guide, archive, checksum, release-body, and remote
  asset identities needed to restore the existing prerelease if publication
  fails.

## 2. Create tracked offline release material

- [x] 2.1 Add
  `packaging/gba-wifi-link/v0.2.0/INSTALL-AND-USAGE.md` with concise neutral
  installation, connection, latency, saving, limitations, troubleshooting,
  privacy, support, and upstream-independence guidance.
- [x] 2.2 Add
  `packaging/gba-wifi-link/v0.2.0/SOURCE-AND-PROVENANCE.md` identifying the
  exact tag, source commit and URL, core and fixture hashes, licence, fixture
  status, and documentation-only repack boundary.
- [x] 2.3 Cross-check both files against current README, `SUPPORT.md`,
  `SECURITY.md`, `docs/gba-wifi-link.md`, and the v0.2.0 release description;
  resolve every material conflict.
- [x] 2.4 Audit the tracked release inputs for private device nicknames,
  qualification-specific model instructions, serials, addresses, local paths,
  commercial content, saves, raw inputs, and unpublished evidence.

## 3. Assemble and verify the corrected artifact set

- [x] 3.1 Create a fresh temporary staging directory and copy into it only the
  hash-verified original core and fixtures, the two tracked release texts, and
  the repository root `LICENSE`.
- [x] 3.2 Generate an internal `SHA256SUMS` covering every staged archive member
  except itself, using stable relative filenames and no private paths.
- [x] 3.3 Build `mgba-gba-wifi-link-v0.2.0-android-arm64.zip` with a deterministic
  file order and timestamps, then prove a clean reconstruction is byte-identical.
- [x] 3.4 Generate the standalone release `SHA256SUMS` covering the standalone
  core, both fixtures, standalone guide, and corrected archive, but not itself.
- [x] 3.5 Verify the exact archive allow-list, uniqueness of every member,
  internal and external checksum scopes, immutable payload hashes, text privacy,
  licence inclusion, and exact source pointer in a clean directory.
- [x] 3.6 Add a sanitized public-alpha readiness record documenting the old and
  corrected documentation/archive identities, the preserved executable
  identities, exact reconstruction commands, and rollback boundary.

## 4. Validate the repository change

- [ ] 4.1 Run strict OpenSpec validation for
  `prepare-public-alpha-sharing` and resolve every structural or semantic
  failure.
- [ ] 4.2 Run repository documentation and boundary-policy checks applicable to
  the new packaging inputs, including a clean privacy-pattern audit.
- [ ] 4.3 Review the repository diff and prove it contains no production source,
  protocol, fixture, generated binary, or unrelated roadmap change.
- [ ] 4.4 Open a focused pull request, run the protected checks, and obtain
  review of the packaging content, checksum contract, and recoverable
  publication sequence before touching the GitHub release.

## 5. Correct the v0.2.0 prerelease

- [ ] 5.1 Reconfirm that the complete corrected local set passes every identity,
  archive, checksum, privacy, and source-provenance check immediately before
  remote mutation.
- [ ] 5.2 Make the prerelease temporarily non-advertised during replacement when
  GitHub supports doing so without changing its tag or source identity.
- [ ] 5.3 Replace the stale standalone guide, archive, and checksum manifest while
  preserving the byte-identical standalone core and fixture assets.
- [ ] 5.4 Update the release description with a visible documentation-only repack
  disclosure, preserved source/core/fixture identities, and corrected guide,
  archive, and checksum identities.
- [ ] 5.5 Redownload every public asset into a new empty directory and repeat the
  complete external and extracted verification before restoring normal
  prerelease visibility or sharing its link.
- [ ] 5.6 If any replacement or verification step fails, restore the preserved
  original release body and assets or keep the release unadvertised; never
  knowingly leave a mixed public asset set.

## 6. Rehearse newcomer onboarding

- [ ] 6.1 Give one person who did not author the instructions the corrected
  public release link, without additional setup guidance, and start the
  twenty-minute interactive time box after downloads are available.
- [ ] 6.2 Let automation verify the downloaded artifact identity while the human
  owns RetroArch installation, host/join navigation, and confirmation of one
  short ready link session, preferring the redistributable fixture.
- [ ] 6.3 Record only the sanitized pass/fail outcome, elapsed onboarding time,
  and concrete documentation friction; do not retain device identifiers,
  addresses, private paths, raw logs, commercial data, or input history.
- [ ] 6.4 If undocumented maintainer knowledge is required, correct and republish
  the affected guide/archive/checksum assets and repeat only the failed
  onboarding portion rather than extending into gameplay qualification.

## 7. Close the public-alpha readiness change

- [ ] 7.1 Update the readiness record with the clean remote verification and
  newcomer result, and state that limited experimental-alpha sharing is open
  while broad report solicitation still depends on issue #20.
- [ ] 7.2 Confirm the final release wording retains Android ARM64, exactly two
  players, Multi-Pak, trusted local network, same effective ROM, unsupported
  features, privacy, and independent-fork limitations.
- [ ] 7.3 Complete final review and protected checks on the immutable repository
  head without manufacturing another device soak or commercial-game run.
- [ ] 7.4 Sync the `public-alpha-distribution` capability, archive the OpenSpec
  change, merge through protected `master`, and share the repository/release
  link only after the corrected public downloads still verify.
