# Prepare Public Alpha Sharing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct the published v0.2.0 documentation bundle, prove its binary
identity and newcomer usability, and open limited public-alpha sharing without
changing runtime behavior.

**Architecture:** Version-specific, reviewed text inputs live under
`packaging/gba-wifi-link/v0.2.0/`; the original published executable and CC0
fixtures remain immutable inputs. A clean staging process creates internal and
external checksum scopes, publication uses a preserved rollback snapshot, and
one short human-owned cold-reader rehearsal closes the onboarding gate.

**Tech Stack:** Markdown, POSIX shell utilities, Python 3 `zipfile`, SHA-256,
Git, GitHub CLI, OpenSpec, existing protected GitHub Actions.

## Global Constraints

- Preserve source tag `v0.2.0` and commit
  `86cc1c26eaf26b5024689c5cb723aa6152efb795`.
- Preserve Android core SHA-256
  `b16873176e358883acc7e7cb8f0312b9a275610a6a9e84c87e092accc3748910`.
- Preserve `gba-link-test.gba` SHA-256
  `2f662e4bcf2ac81c438ae5eacc786b2d2984c00807d3656b4990da66a99edc13`.
- Preserve `gba-link-continuous.gba` SHA-256
  `c1fe01752d4f5863d6e3e1a9866b061aaadf2927ccc3df31ba9ecbf4bc68fe9d`.
- Do not rebuild or modify the core or fixtures.
- Do not modify emulator, protocol, SIO, RTC, save, latency, audio, video, or
  lifecycle behavior.
- Do not retain private device nicknames, serials, addresses, private paths,
  commercial data, saves, raw logs, or input histories in tracked or published
  evidence.
- Keep v0.2.1 issues #20 through #23 outside this implementation.
- Do not replace a GitHub release asset until the complete corrected local set
  passes verification and the original set has a recoverable snapshot.

---

### Task 1: Capture and verify the public rollback baseline

**Files:**
- Create outside Git: a uniquely named
  `build-public-alpha-rollback-v0.2.0.*` directory
- Read: `openspec/changes/prepare-public-alpha-sharing/design.md`
- Read: `openspec/changes/prepare-public-alpha-sharing/specs/public-alpha-distribution/spec.md`

**Interfaces:**
- Consumes: GitHub release `Aelvryx/mgba-wifi-link@v0.2.0`.
- Produces: a private verified rollback directory containing original assets,
  `release.json`, `release-body.md`, and `assets.json`.

- [ ] **Step 1: Confirm the working tree and release identity**

  Run:

  ```bash
  git status --short
  git rev-parse v0.2.0^{}
  gh release view v0.2.0 --repo Aelvryx/mgba-wifi-link \
    --json tagName,isDraft,isPrerelease,url
  ```

  Expected: the tag resolves to
  `86cc1c26eaf26b5024689c5cb723aa6152efb795`; the GitHub release is a
  prerelease for `v0.2.0`.

- [ ] **Step 2: Create the private rollback directory**

  Run:

  ```bash
  readiness_rollback_dir=$(mktemp -d \
    "$PWD/build-public-alpha-rollback-v0.2.0.XXXXXX")
  printf '%s\n' "$readiness_rollback_dir"
  ```

  Expected: one explicit directory beneath the workspace; retain its printed
  path until remote verification and cold-reader work finish.

- [ ] **Step 3: Snapshot release metadata and assets**

  Run with `readiness_rollback_dir` set to the directory from Step 2:

  ```bash
  gh release view v0.2.0 --repo Aelvryx/mgba-wifi-link \
    --json name,body,isDraft,isPrerelease,tagName,publishedAt,url,assets \
    > "$readiness_rollback_dir/release.json"
  gh release view v0.2.0 --repo Aelvryx/mgba-wifi-link \
    --json body --jq .body > "$readiness_rollback_dir/release-body.md"
  gh api repos/Aelvryx/mgba-wifi-link/releases/tags/v0.2.0 \
    --jq .assets > "$readiness_rollback_dir/assets.json"
  gh release download v0.2.0 --repo Aelvryx/mgba-wifi-link \
    --dir "$readiness_rollback_dir/assets"
  ```

  Expected: all six original assets are present: core, two fixtures, guide,
  archive, and `SHA256SUMS`.

- [ ] **Step 4: Verify the original standalone and archive manifests**

  Run:

  ```bash
  (cd "$readiness_rollback_dir/assets" && sha256sum -c SHA256SUMS)
  mkdir "$readiness_rollback_dir/extracted"
  unzip -q \
    "$readiness_rollback_dir/assets/mgba-gba-wifi-link-v0.2.0-android-arm64.zip" \
    -d "$readiness_rollback_dir/extracted"
  (cd "$readiness_rollback_dir/extracted" && sha256sum -c SHA256SUMS)
  ```

  Expected: every original check reports `OK`.

- [ ] **Step 5: Assert the three immutable payload hashes**

  Run:

  ```bash
  printf '%s  %s\n' \
    b16873176e358883acc7e7cb8f0312b9a275610a6a9e84c87e092accc3748910 \
    "$readiness_rollback_dir/assets/mgba_libretro_android.so" \
    2f662e4bcf2ac81c438ae5eacc786b2d2984c00807d3656b4990da66a99edc13 \
    "$readiness_rollback_dir/assets/gba-link-test.gba" \
    c1fe01752d4f5863d6e3e1a9866b061aaadf2927ccc3df31ba9ecbf4bc68fe9d \
    "$readiness_rollback_dir/assets/gba-link-continuous.gba" \
    | sha256sum -c -
  ```

  Expected: all three checks report `OK`; stop the change if any differs.

### Task 2: Add reviewable v0.2.0 release inputs

**Files:**
- Create: `packaging/gba-wifi-link/v0.2.0/INSTALL-AND-USAGE.md`
- Create: `packaging/gba-wifi-link/v0.2.0/SOURCE-AND-PROVENANCE.md`
- Read: `README.md`
- Read: `SUPPORT.md`
- Read: `SECURITY.md`
- Read: `docs/gba-wifi-link.md`
- Read: `UPSTREAM.md`

**Interfaces:**
- Consumes: current product/support/security guidance and immutable identities
  from Task 1.
- Produces: the exact guide and provenance notice copied into the corrected
  archive.

- [ ] **Step 1: Create the version-specific packaging directory**

  Run:

  ```bash
  mkdir -p packaging/gba-wifi-link/v0.2.0
  ```

  Expected: only the new packaging directory is created.

- [ ] **Step 2: Write the offline installation guide**

  Create `packaging/gba-wifi-link/v0.2.0/INSTALL-AND-USAGE.md` with these
  exact top-level sections:

  ```markdown
  # GBA Wi-Fi Link v0.2.0 — Install and Usage

  ## What this alpha is
  ## Requirements and limits
  ## Install on Android ARM64
  ## Host and join
  ## Latency policy
  ## Saves and states
  ## Troubleshooting
  ## Privacy and reporting
  ## Source, licence, and support
  ```

  The text must say: exactly two players; Multi-Pak only; stock RetroArch;
  trusted local LAN or direct Android hotspot; identical effective ROM bytes;
  Auto (Stable) is default; live-session savestates are rejected; the fork is
  independent and unsupported by upstream mGBA; commercial ROMs are not
  distributed.

- [ ] **Step 3: Write the source and provenance notice**

  Create `packaging/gba-wifi-link/v0.2.0/SOURCE-AND-PROVENANCE.md` containing:

  ```markdown
  # Source and Provenance

  - Repository: https://github.com/Aelvryx/mgba-wifi-link
  - Release tag: `v0.2.0`
  - Source commit: `86cc1c26eaf26b5024689c5cb723aa6152efb795`
  - Licence: Mozilla Public License 2.0; see `LICENSE`
  - Android core SHA-256: `b16873176e358883acc7e7cb8f0312b9a275610a6a9e84c87e092accc3748910`
  - `gba-link-test.gba` SHA-256: `2f662e4bcf2ac81c438ae5eacc786b2d2984c00807d3656b4990da66a99edc13`
  - `gba-link-continuous.gba` SHA-256: `c1fe01752d4f5863d6e3e1a9866b061aaadf2927ccc3df31ba9ecbf4bc68fe9d`

  The core and fixtures above are byte-identical to the original v0.2.0
  prerelease. The guide, archive, and checksum assets were repacked to align
  public documentation, privacy, licence, and provenance; no executable was
  rebuilt.
  ```

- [ ] **Step 4: Compare release inputs with current public guidance**

  Run:

  ```bash
  rg -n "two players|Multi-Pak|Android ARM64|Auto \(Stable\)|savestate|trusted|upstream|commercial" \
    README.md SUPPORT.md SECURITY.md docs/gba-wifi-link.md \
    packaging/gba-wifi-link/v0.2.0
  ```

  Expected: every promised behavior in the guide has a current public owner;
  reconcile contradictions in the packaging files, not by broadening runtime
  claims.

- [ ] **Step 5: Run the privacy-pattern audit**

  Run:

  ```bash
  if rg -n -i \
    'Thor|Odin|AYN|adb-[[:alnum:]]|192\.168\.|10\.[0-9]+\.[0-9]+\.[0-9]+|/sdcard/Android/data/|/var/home/|raw input|commercial ROM path' \
    packaging/gba-wifi-link/v0.2.0; then
    exit 1
  fi
  ```

  Expected: no matches.

- [ ] **Step 6: Commit the tracked release inputs**

  Run:

  ```bash
  git add packaging/gba-wifi-link/v0.2.0
  git commit -m "docs: add reviewable v0.2.0 release inputs"
  ```

### Task 3: Build the corrected package deterministically

**Files:**
- Read: `packaging/gba-wifi-link/v0.2.0/INSTALL-AND-USAGE.md`
- Read: `packaging/gba-wifi-link/v0.2.0/SOURCE-AND-PROVENANCE.md`
- Read: `LICENSE`
- Create outside Git: two clean staging trees and the corrected release assets
- Create: `docs/public-alpha-readiness.md`

**Interfaces:**
- Consumes: verified rollback payloads from Task 1 and tracked release text from
  Task 2.
- Produces: one deterministic zip, one external checksum manifest, and a
  readiness record containing the reconstruction and rollback evidence.

- [ ] **Step 1: Create two clean staging roots**

  Run:

  ```bash
  readiness_stage_a=$(mktemp -d "$PWD/build-public-alpha-stage-a.XXXXXX")
  readiness_stage_b=$(mktemp -d "$PWD/build-public-alpha-stage-b.XXXXXX")
  mkdir "$readiness_stage_a/archive" "$readiness_stage_a/release"
  mkdir "$readiness_stage_b/archive" "$readiness_stage_b/release"
  ```

- [ ] **Step 2: Populate both archive roots in fixed order**

  Run the following loop with `readiness_rollback_dir` still pointing at the
  verified Task 1 snapshot:

  ```bash
  for readiness_stage_dir in "$readiness_stage_a" "$readiness_stage_b"; do
    cp "$readiness_rollback_dir/assets/mgba_libretro_android.so" \
      "$readiness_stage_dir/archive/"
    cp "$readiness_rollback_dir/assets/gba-link-test.gba" \
      "$readiness_stage_dir/archive/"
    cp "$readiness_rollback_dir/assets/gba-link-continuous.gba" \
      "$readiness_stage_dir/archive/"
    cp packaging/gba-wifi-link/v0.2.0/INSTALL-AND-USAGE.md \
      "$readiness_stage_dir/archive/"
    cp packaging/gba-wifi-link/v0.2.0/SOURCE-AND-PROVENANCE.md \
      "$readiness_stage_dir/archive/"
    cp LICENSE "$readiness_stage_dir/archive/"
    (cd "$readiness_stage_dir/archive" && sha256sum \
      mgba_libretro_android.so \
      gba-link-test.gba \
      gba-link-continuous.gba \
      INSTALL-AND-USAGE.md \
      SOURCE-AND-PROVENANCE.md \
      LICENSE > SHA256SUMS)
  done
  ```

- [ ] **Step 3: Create each zip with frozen metadata**

  Run once for each staging root:

  ```bash
  for readiness_stage_dir in "$readiness_stage_a" "$readiness_stage_b"; do
    READINESS_ARCHIVE_DIR="$readiness_stage_dir/archive" \
    READINESS_ZIP_PATH="$readiness_stage_dir/release/mgba-gba-wifi-link-v0.2.0-android-arm64.zip" \
    python3 - <<'PY'
  import os
  import pathlib
  import zipfile

  source = pathlib.Path(os.environ["READINESS_ARCHIVE_DIR"])
  target = pathlib.Path(os.environ["READINESS_ZIP_PATH"])
  names = [
      "mgba_libretro_android.so",
      "gba-link-test.gba",
      "gba-link-continuous.gba",
      "INSTALL-AND-USAGE.md",
      "SOURCE-AND-PROVENANCE.md",
      "LICENSE",
      "SHA256SUMS",
  ]
  with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED,
                       compresslevel=9) as archive:
      for name in names:
          info = zipfile.ZipInfo(name, (2026, 8, 5, 0, 0, 0))
          info.compress_type = zipfile.ZIP_DEFLATED
          info.create_system = 3
          info.external_attr = 0o100644 << 16
          archive.writestr(info, (source / name).read_bytes(), compresslevel=9)
  PY
  done
  ```

  Expected: both archives contain the same seven members and no directory
  prefix or host path.

- [ ] **Step 4: Prove deterministic reconstruction**

  Run:

  ```bash
  sha256sum \
    "$readiness_stage_a/release/mgba-gba-wifi-link-v0.2.0-android-arm64.zip" \
    "$readiness_stage_b/release/mgba-gba-wifi-link-v0.2.0-android-arm64.zip"
  cmp \
    "$readiness_stage_a/release/mgba-gba-wifi-link-v0.2.0-android-arm64.zip" \
    "$readiness_stage_b/release/mgba-gba-wifi-link-v0.2.0-android-arm64.zip"
  ```

  Expected: identical SHA-256 values and `cmp` exit status 0.

- [ ] **Step 5: Assemble standalone release assets and manifest**

  Run:

  ```bash
  cp "$readiness_rollback_dir/assets/mgba_libretro_android.so" \
    "$readiness_stage_a/release/"
  cp "$readiness_rollback_dir/assets/gba-link-test.gba" \
    "$readiness_stage_a/release/"
  cp "$readiness_rollback_dir/assets/gba-link-continuous.gba" \
    "$readiness_stage_a/release/"
  cp packaging/gba-wifi-link/v0.2.0/INSTALL-AND-USAGE.md \
    "$readiness_stage_a/release/"
  (cd "$readiness_stage_a/release" && sha256sum \
    mgba_libretro_android.so \
    gba-link-test.gba \
    gba-link-continuous.gba \
    INSTALL-AND-USAGE.md \
    mgba-gba-wifi-link-v0.2.0-android-arm64.zip > SHA256SUMS)
  ```

- [ ] **Step 6: Verify the complete local set in a third clean directory**

  Run:

  ```bash
  readiness_verify_dir=$(mktemp -d "$PWD/build-public-alpha-verify.XXXXXX")
  cp "$readiness_stage_a/release/"* "$readiness_verify_dir/"
  (cd "$readiness_verify_dir" && sha256sum -c SHA256SUMS)
  mkdir "$readiness_verify_dir/extracted"
  unzip -q \
    "$readiness_verify_dir/mgba-gba-wifi-link-v0.2.0-android-arm64.zip" \
    -d "$readiness_verify_dir/extracted"
  (cd "$readiness_verify_dir/extracted" && sha256sum -c SHA256SUMS)
  find "$readiness_verify_dir/extracted" -maxdepth 1 -type f -printf '%f\n' \
    | LC_ALL=C sort
  ```

  Expected: both manifests pass; the sorted archive list is exactly
  `INSTALL-AND-USAGE.md`, `LICENSE`, `SHA256SUMS`,
  `SOURCE-AND-PROVENANCE.md`, both fixture names, and the core name.

- [ ] **Step 7: Write the readiness record**

  Create `docs/public-alpha-readiness.md` with these sections:

  ```markdown
  # v0.2.0 Public Alpha Readiness

  ## Scope
  ## Preserved source and executable identities
  ## Documentation-only repack
  ## Corrected artifact identities
  ## Reconstruction and verification
  ## Publication rollback
  ## Newcomer rehearsal
  ## Sharing boundary
  ```

  Record the actual new guide, archive, and standalone-manifest hashes from
  `readiness_stage_a/release/SHA256SUMS`; record old guide hash
  `69eafbef57876bedc8acb17163886de3f587f19a42fa00d6440d3838b4f085f1`
  and old archive hash
  `8ba4c62975f11bb8a52109a2538d811fb868d9e8ebf31aa7f09d9e18808dd4fc`.
  Mark publication and newcomer sections as pending in factual prose without
  using placeholder tokens.

- [ ] **Step 8: Commit the reconstruction evidence**

  Run:

  ```bash
  git add docs/public-alpha-readiness.md
  git commit -m "docs: record public alpha repack evidence"
  ```

### Task 4: Validate and review before remote mutation

**Files:**
- Modify if required: packaging files and `docs/public-alpha-readiness.md`
- Read: `.github/workflows/gba-wifi-link-ci.yml`
- Read: `tools/audit-gba-wifi-link-boundary.py`

**Interfaces:**
- Consumes: committed release inputs/evidence and local artifacts from Tasks 2
  and 3.
- Produces: a reviewed PR head and green protected checks authorizing the
  release-only mutation.

- [ ] **Step 1: Run strict OpenSpec validation**

  Run:

  ```bash
  openspec validate prepare-public-alpha-sharing --strict
  ```

  Expected: validation passes.

- [ ] **Step 2: Run current product-boundary and privacy checks**

  Run:

  ```bash
  python3 tools/audit-gba-wifi-link-boundary.py --source-root .
  if rg -n -i \
    'Thor|Odin|AYN|adb-[[:alnum:]]|192\.168\.|10\.[0-9]+\.[0-9]+\.[0-9]+|/sdcard/Android/data/|/var/home/' \
    packaging/gba-wifi-link/v0.2.0 docs/public-alpha-readiness.md; then
    exit 1
  fi
  ```

  Expected: boundary audit passes and privacy grep finds nothing.

- [ ] **Step 3: Prove the production tree and immutable payloads did not change**

  Run:

  ```bash
  git diff --stat master...HEAD
  git diff --name-only master...HEAD
  git diff --exit-code master...HEAD -- \
    src include CMakeLists.txt .github/workflows/gba-wifi-link-ci.yml \
    src/platform/test/fixtures
  ```

  Expected: only OpenSpec, packaging, and readiness documentation are changed;
  the restricted production/test-fixture paths have no diff.

- [ ] **Step 4: Push a focused branch and open a draft PR**

  Run:

  ```bash
  git push -u origin HEAD
  gh pr create --repo Aelvryx/mgba-wifi-link --draft --fill
  ```

  Expected: a draft PR explicitly states that the core and fixtures are
  byte-identical and that remote release mutation waits for review.

- [ ] **Step 5: Wait for and inspect protected checks**

  Run:

  ```bash
  gh pr checks --repo Aelvryx/mgba-wifi-link --watch
  ```

  Expected: all required checks pass on the current head.

- [ ] **Step 6: Obtain focused review**

  Review must confirm the offline guidance, source/licence notice, checksum
  scopes, immutable payload proof, rollback snapshot, and exact publication
  sequence. Apply review corrections, rerun Steps 1–5, and commit each coherent
  correction before proceeding.

### Task 5: Replace and independently verify the public assets

**Files:**
- Modify remotely: GitHub release `v0.2.0` body and three replaceable assets
- Modify: `docs/public-alpha-readiness.md`
- Preserve outside Git: Task 1 rollback directory

**Interfaces:**
- Consumes: reviewed local release directory in `readiness_stage_a/release`.
- Produces: one coherent corrected public prerelease verified by clean
  redownload, plus exact public asset hashes in the readiness record.

- [ ] **Step 1: Re-run local verification immediately before publication**

  Run:

  ```bash
  (cd "$readiness_stage_a/release" && sha256sum -c SHA256SUMS)
  printf '%s  %s\n' \
    b16873176e358883acc7e7cb8f0312b9a275610a6a9e84c87e092accc3748910 \
    "$readiness_stage_a/release/mgba_libretro_android.so" \
    2f662e4bcf2ac81c438ae5eacc786b2d2984c00807d3656b4990da66a99edc13 \
    "$readiness_stage_a/release/gba-link-test.gba" \
    c1fe01752d4f5863d6e3e1a9866b061aaadf2927ccc3df31ba9ecbf4bc68fe9d \
    "$readiness_stage_a/release/gba-link-continuous.gba" \
    | sha256sum -c -
  ```

  Expected: all checks pass. Stop before any `gh release upload` on failure.

- [ ] **Step 2: Prepare the exact corrected release body**

  Copy the preserved original body to
  `$readiness_stage_a/release/release-body-corrected.md`. Add a prominent note:

  ```markdown
  > Documentation-only asset correction: the offline guide, archive, and
  > checksum manifest were repacked after publication to align public privacy,
  > licensing, and source-provenance material. The `v0.2.0` source commit,
  > Android core, and both CC0 fixtures are byte-identical to the original
  > prerelease. The hashes below identify the corrected public assets.
  ```

  Replace the old guide/archive/checksum hashes with the exact new values and
  retain the source/core/fixture identities.

- [ ] **Step 3: Hide the release during the non-atomic replacement**

  Run:

  ```bash
  release_id=$(gh api repos/Aelvryx/mgba-wifi-link/releases/tags/v0.2.0 --jq .id)
  gh api --method PATCH \
    "repos/Aelvryx/mgba-wifi-link/releases/$release_id" \
    -F draft=true -F prerelease=true >/dev/null
  ```

  Expected: release API reports `draft: true`. If GitHub rejects conversion to
  draft, stop without replacing assets and record that the fallback publication
  sequence needs review.

- [ ] **Step 4: Replace only the mutable assets**

  Run:

  ```bash
  gh release upload v0.2.0 --repo Aelvryx/mgba-wifi-link --clobber \
    "$readiness_stage_a/release/INSTALL-AND-USAGE.md" \
    "$readiness_stage_a/release/mgba-gba-wifi-link-v0.2.0-android-arm64.zip" \
    "$readiness_stage_a/release/SHA256SUMS"
  gh release edit v0.2.0 --repo Aelvryx/mgba-wifi-link \
    --notes-file "$readiness_stage_a/release/release-body-corrected.md"
  ```

  Expected: core and fixture asset IDs remain present; only the guide, archive,
  and checksum asset content changes.

- [ ] **Step 5: Redownload and verify while still unadvertised**

  Run:

  ```bash
  readiness_remote_dir=$(mktemp -d "$PWD/build-public-alpha-remote.XXXXXX")
  gh release download v0.2.0 --repo Aelvryx/mgba-wifi-link \
    --dir "$readiness_remote_dir"
  (cd "$readiness_remote_dir" && sha256sum -c SHA256SUMS)
  mkdir "$readiness_remote_dir/extracted"
  unzip -q \
    "$readiness_remote_dir/mgba-gba-wifi-link-v0.2.0-android-arm64.zip" \
    -d "$readiness_remote_dir/extracted"
  (cd "$readiness_remote_dir/extracted" && sha256sum -c SHA256SUMS)
  cmp "$readiness_stage_a/release/INSTALL-AND-USAGE.md" \
    "$readiness_remote_dir/INSTALL-AND-USAGE.md"
  cmp \
    "$readiness_stage_a/release/mgba-gba-wifi-link-v0.2.0-android-arm64.zip" \
    "$readiness_remote_dir/mgba-gba-wifi-link-v0.2.0-android-arm64.zip"
  ```

  Expected: manifests pass and public downloads are byte-identical to the
  reviewed local set.

- [ ] **Step 6: Republish the verified prerelease**

  Run:

  ```bash
  gh api --method PATCH \
    "repos/Aelvryx/mgba-wifi-link/releases/$release_id" \
    -F draft=false -F prerelease=true >/dev/null
  gh release view v0.2.0 --repo Aelvryx/mgba-wifi-link \
    --json isDraft,isPrerelease,url,assets
  ```

  Expected: `isDraft` is false, `isPrerelease` is true, and six coherent assets
  are listed.

- [ ] **Step 7: Exercise rollback if any publication step failed**

  On any failure after Step 3, keep the release as a draft and restore the
  preserved body and all six files using:

  ```bash
  gh release upload v0.2.0 --repo Aelvryx/mgba-wifi-link --clobber \
    "$readiness_rollback_dir/assets/"*
  gh release edit v0.2.0 --repo Aelvryx/mgba-wifi-link \
    --notes-file "$readiness_rollback_dir/release-body.md"
  ```

  Redownload and verify the restored original manifests before republishing.
  Do not continue to newcomer testing until the corrected set succeeds later.

- [ ] **Step 8: Record remote verification and commit**

  Update `docs/public-alpha-readiness.md` with the public asset hashes, release
  URL, clean-redownload result, and publication date. Run the privacy audit,
  then commit:

  ```bash
  git add docs/public-alpha-readiness.md
  git commit -m "docs: record corrected v0.2.0 assets"
  git push
  ```

### Task 6: Run the cold-reader onboarding rehearsal

**Files:**
- Modify: `docs/public-alpha-readiness.md`
- Read publicly: GitHub release `v0.2.0` and its corrected guide

**Interfaces:**
- Consumes: clean-redownload-verified public release from Task 5.
- Produces: sanitized onboarding evidence or a bounded documentation correction.

- [ ] **Step 1: Select and brief the reader without supplying setup steps**

  Provide only the public release URL and say that the exercise tests the
  instructions, not the reader. Do not provide device-specific advice or a
  private copy of the guide.

- [ ] **Step 2: Verify the reader's downloaded artifact**

  Automation checks the downloaded `SHA256SUMS` against the selected core or
  bundle before interaction. Expected: the public hash matches the readiness
  record.

- [ ] **Step 3: Run the twenty-minute human-owned onboarding window**

  Start timing after downloads are available. The reader owns RetroArch core
  installation, content loading, host/join, and confirmation of one ready
  session. Use a supplied CC0 fixture where practical. The maintainer observes
  but does not inject controller input or silently provide a missing step.

- [ ] **Step 4: Classify the result**

  Record exactly one of:

  ```text
  PASS — installed exact public core and established one ready link session
  DOCS DEFECT — a necessary step was absent or materially ambiguous
  ENVIRONMENT BLOCK — unrelated frontend, network, or hardware condition
  ```

  Also record elapsed minutes and a concise description of actionable friction;
  retain no private identifiers or raw evidence.

- [ ] **Step 5: Correct only genuine documentation defects**

  If the result is `DOCS DEFECT`, edit the tracked guide, rebuild both manifests
  and the deterministic archive through Task 3, republish through Task 5, and
  repeat only the failed onboarding portion. An `ENVIRONMENT BLOCK` gets one
  documented retry after the unrelated condition is removed; it does not
  authorize exploratory gameplay automation.

- [ ] **Step 6: Record the passed gate and commit**

  Update `docs/public-alpha-readiness.md` with the sanitized classification,
  elapsed time, fixture/commercial-content category without a private filename,
  and any documentation correction. Commit and push:

  ```bash
  git add packaging/gba-wifi-link/v0.2.0 docs/public-alpha-readiness.md
  git commit -m "docs: close public alpha onboarding gate"
  git push
  ```

### Task 7: Land the readiness contract and open limited sharing

**Files:**
- Modify: `openspec/changes/prepare-public-alpha-sharing/tasks.md`
- Sync: `openspec/specs/public-alpha-distribution/spec.md`
- Archive: `openspec/changes/prepare-public-alpha-sharing/`
- Read: `README.md`, `ROADMAP.md`, `SUPPORT.md`, `SECURITY.md`

**Interfaces:**
- Consumes: corrected verified public assets, passed newcomer gate, reviewed PR.
- Produces: authoritative distribution capability, archived change, protected
  master merge, and an honest public share boundary.

- [ ] **Step 1: Check every completed task against evidence**

  Mark a task complete only when its file, command output, GitHub state, or
  sanitized human result exists. Leave no task checked merely because it became
  unnecessary; describe an explicit not-applicable result in the readiness
  record first.

- [ ] **Step 2: Run final validation on the immutable head**

  Run:

  ```bash
  openspec validate prepare-public-alpha-sharing --strict
  python3 tools/audit-gba-wifi-link-boundary.py --source-root .
  git diff --exit-code master...HEAD -- src include CMakeLists.txt
  gh pr checks --repo Aelvryx/mgba-wifi-link --watch
  ```

  Expected: strict validation, boundary audit, production-tree proof, and all
  protected checks pass. No commercial replay is added.

- [ ] **Step 3: Sync the new capability**

  Invoke `$openspec-sync-specs` for `prepare-public-alpha-sharing` and verify
  `openspec/specs/public-alpha-distribution/spec.md` contains every approved
  requirement and scenario.

- [ ] **Step 4: Archive the completed change**

  Invoke `$openspec-archive-change` for `prepare-public-alpha-sharing`, rerun
  strict OpenSpec validation, commit the synced/archive result, and push it to
  the existing PR.

- [ ] **Step 5: Complete review and merge through protected master**

  Mark the PR ready only after the final checks pass, merge through the required
  protected path, and verify `origin/master` contains the authoritative spec,
  tracked packaging inputs, and readiness evidence.

- [ ] **Step 6: Share with the approved audience**

  Link the repository or v0.2.0 release as an experimental Android ARM64
  two-player Multi-Pak alpha. Retain the trusted-LAN, same-ROM, unsupported
  feature, privacy, and upstream-independence wording. Do not begin a broad
  troubleshooting-report campaign until issue #20 supplies the separately
  reviewed sanitized diagnostic workflow.
