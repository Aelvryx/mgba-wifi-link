# Automated Release Artifacts and Provenance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make one approved annotated version tag automatically validate, reproducibly build, package, attest, verify, and publish an immutable Android ARM64 GBA Wi-Fi Link prerelease.

**Architecture:** A standard-library Python package owns tag admission, canonical metadata, deterministic packaging, verification, privacy, and a mockable GitHub publisher. The protected CI workflow remains the single validation owner and is composed by a tag-triggered release workflow; all build jobs are read-only, while one final publisher consumes an immutable verified artifact, transactionally stages a private draft, and publishes it automatically.

**Tech Stack:** Python 3 standard library (`dataclasses`, `hashlib`, `json`, `pathlib`, `subprocess`, `zipfile`, `unittest`), Bash, Git, GitHub Actions, GitHub CLI/API, Android NDK `27.2.12479018`, CMake/Ninja, SHA-256, GitHub artifact attestations, OpenSpec.

## Global Constraints

- The release trigger accepts only a new annotated `vMAJOR.MINOR.PATCH` tag whose peeled commit is reachable from protected `master`; `v0.x` is always a prerelease.
- Pushing the approved tag is the only maintainer release action. No manual dispatch, approval, asset upload, draft pause, or Publish click follows it.
- Identical tagged inputs must produce byte-identical core, rendered documents, manifests, checksums, and ZIP payloads; signed attestation envelopes remain outside reproducible/checksum scopes.
- The public set has exactly seven project assets. The archive and standalone checksum scopes are those frozen in `specs/automated-release-provenance/spec.md`.
- Validation/build/package jobs are read-only. Only the publisher gets the minimum `contents`, `attestations`, and `id-token` write permissions, and it cannot check out or build source.
- Publication is fail-closed and immutable. Exact reruns are read-only success; conflicts fail; corrections use a new tag/version.
- Release artifacts must not contain ROM/BIOS identities, saves, raw inputs, endpoint/frontend logs, paths, addresses, device identities, commercial evidence, or secrets.
- The historical `packaging/gba-wifi-link/v0.2.0/` tree and live v0.2.0 release are not mutated.
- This change does not create a production version tag or publish v0.2.1.
- The repository neither solicits nor forbids unsolicited feedback and does not promise support.

---

### Task 1: Freeze the release contract and synthetic fixtures

**Files:**
- Create: `packaging/gba-wifi-link/release/contract-v1.json`
- Create: `packaging/gba-wifi-link/release/templates/INSTALL-AND-USAGE.md.in`
- Create: `packaging/gba-wifi-link/release/templates/SOURCE-AND-PROVENANCE.md.in`
- Create: `packaging/gba-wifi-link/release/templates/RELEASE-BODY.md.in`
- Create: `tools/gba_wifi_link_release/__init__.py`
- Create: `tools/gba_wifi_link_release/model.py`
- Create: `tools/gba_wifi_link_release/fixtures/v0.2.0-history.json`
- Create: `tools/gba_wifi_link_release/fixtures/synthetic/release-notes/v9.8.7.md`
- Create: `tools/gba_wifi_link_release/fixtures/synthetic/input/`
- Create: `tools/gba_wifi_link_release/tests/test_contract.py`
- Preserve: `packaging/gba-wifi-link/v0.2.0/`

**Interfaces:**
- Consumes: Current v0.2.0 release identities and the asset/member requirements in the delta spec.
- Produces: `ReleaseContext`, `GateResult`, `ReleaseAsset`, `ReleaseSet`, `load_contract(path)`, and immutable synthetic/history fixtures for every later task.

- [ ] **Step 1: Write the failing contract test**

```python
class ContractTest(unittest.TestCase):
    def test_contract_freezes_public_and_archive_members(self):
        contract = load_contract(CONTRACT)
        self.assertEqual(contract.schema, 1)
        self.assertEqual(len(contract.public_assets), 7)
        self.assertEqual(contract.public_assets[-2:],
                         ("RELEASE-PROVENANCE.json", "SHA256SUMS"))
        self.assertEqual(set(contract.archive_members), {
            "mgba_libretro_android.so", "gba-link-test.gba",
            "gba-link-continuous.gba", "INSTALL-AND-USAGE.md",
            "SOURCE-AND-PROVENANCE.md", "LICENSE",
            "BUILD-PROVENANCE.json", "SHA256SUMS",
        })
```

- [ ] **Step 2: Run the test and confirm the missing module/contract failure**

Run: `python3 -m unittest tools.gba_wifi_link_release.tests.test_contract -v`

Expected: FAIL because `model.py` and `contract-v1.json` are not implemented.

- [ ] **Step 3: Add the canonical immutable model**

```python
@dataclass(frozen=True)
class GateResult:
    name: str
    run_id: int
    job_id: int
    conclusion: str

@dataclass(frozen=True)
class ReleaseAsset:
    name: str
    size: int
    sha256: str

@dataclass(frozen=True)
class ReleaseContext:
    repository: str
    tag: str
    tag_object: str
    commit: str
    version: str
    source_date_epoch: int
    prerelease: bool
    gates: tuple[GateResult, ...]

@dataclass(frozen=True)
class ReleaseSet:
    context: ReleaseContext
    assets: tuple[ReleaseAsset, ...]
```

- [ ] **Step 4: Write `contract-v1.json`, version-neutral templates, and public synthetic fixture bytes**

Use the exact names/modes/scopes from the delta spec. Give synthetic inputs literal contents such as `synthetic core v9.8.7\n`; do not copy a commercial ROM, endpoint log, device name, address, or real save.

- [ ] **Step 5: Record the live v0.2.0 historical fixture and prove the tracked tree still matches it**

Run: `gh release view v0.2.0 --repo Aelvryx/mgba-wifi-link --json tagName,targetCommitish,isDraft,isPrerelease,assets`

Then download into a fresh `mktemp -d`, verify standalone/internal `SHA256SUMS`, and store only public identities/hashes in `v0.2.0-history.json`.

- [ ] **Step 6: Run the contract tests and historical-tree comparison**

Run: `python3 -m unittest tools.gba_wifi_link_release.tests.test_contract -v`

Expected: PASS; `git diff --exit-code -- packaging/gba-wifi-link/v0.2.0` is empty.

- [ ] **Step 7: Commit the contract unit**

```bash
git add packaging/gba-wifi-link/release tools/gba_wifi_link_release
git commit -m "release: freeze automated package contract"
```

### Task 2: Implement annotated-tag admission and reviewed release metadata

**Files:**
- Create: `tools/gba_wifi_link_release/admission.py`
- Create: `tools/gba_wifi_link_release/render.py`
- Create: `tools/gba_wifi_link_release/tests/test_admission.py`
- Create: `tools/gba_wifi_link_release/tests/test_render.py`
- Modify: `tools/gba_wifi_link_release/model.py`

**Interfaces:**
- Consumes: `ReleaseContext`, contract-v1, a local full Git checkout, exact workflow/gate JSON, and `packaging/gba-wifi-link/releases/{tag}/RELEASE-NOTES.md` where `{tag}` is `ReleaseContext.tag`.
- Produces: `admit_release(repo: Path, tag: str, evidence: Mapping[str, object]) -> ReleaseContext`, `verify_remote_tag(context, repo: str) -> None`, and `render_release_body(context, notes: str) -> bytes`.

- [ ] **Step 1: Write failing valid/invalid tag tests with temporary Git repositories**

```python
def test_annotated_reachable_tag_is_admitted(self):
    repo = make_repo_with_annotated_tag("v9.8.7")
    context = admit_release(repo, "v9.8.7", VALID_EVIDENCE)
    self.assertEqual(context.version, "9.8.7")
    self.assertTrue(context.prerelease)
    self.assertNotEqual(context.tag_object, context.commit)
```

Cover lightweight tags, noncanonical names, off-master commits, missing notes, placeholders, conflicting version text, non-success gates, and tag-object/commit substitution.

- [ ] **Step 2: Run admission tests and verify they fail**

Run: `python3 -m unittest tools.gba_wifi_link_release.tests.test_admission -v`

Expected: FAIL because `admission.py` does not exist.

- [ ] **Step 3: Implement strict tag parsing and local Git verification**

```python
TAG_RE = re.compile(r"^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")

def require_annotated_tag(repo: Path, tag: str) -> tuple[str, str]:
    if not TAG_RE.fullmatch(tag):
        raise AdmissionError("TAG_FORMAT")
    tag_object = git(repo, "rev-parse", f"refs/tags/{tag}")
    if git(repo, "cat-file", "-t", tag_object) != "tag":
        raise AdmissionError("TAG_NOT_ANNOTATED")
    commit = git(repo, "rev-parse", f"refs/tags/{tag}^{{commit}}")
    git(repo, "merge-base", "--is-ancestor", commit, "origin/master")
    return tag_object, commit
```

- [ ] **Step 4: Implement exact protected-evidence and notes validation**

Require the six canonical job names, `success` conclusions, the peeled commit, numeric run/job IDs, no unresolved `TBD`, `TODO`, angle-bracket placeholder, or generated-field override, and `v0.x => prerelease=true`.

- [ ] **Step 5: Write and run release-body rendering tests**

Assert UTF-8/LF output, stable identity order, exact notes preservation, generated tag/commit/workflow/asset sections, no placeholder, and no identity value supplied by notes.

Run: `python3 -m unittest tools.gba_wifi_link_release.tests.test_render -v`

- [ ] **Step 6: Implement deterministic rendering and remote-tag recheck inputs**

`verify_remote_tag` must compare both `refs/tags/{context.tag}` and
`refs/tags/{context.tag}^{commit}` with `context.tag_object` and
`context.commit`; never accept only the peeled commit.

- [ ] **Step 7: Run both modules and commit**

```bash
python3 -m unittest tools.gba_wifi_link_release.tests.test_admission tools.gba_wifi_link_release.tests.test_render -v
git add tools/gba_wifi_link_release
git commit -m "release: validate annotated release tags"
```

### Task 3: Implement canonical provenance and privacy validation

**Files:**
- Create: `tools/gba_wifi_link_release/provenance.py`
- Create: `tools/gba_wifi_link_release/privacy.py`
- Create: `tools/gba_wifi_link_release/tests/test_provenance.py`
- Create: `tools/gba_wifi_link_release/tests/test_privacy.py`
- Modify: `packaging/gba-wifi-link/release/contract-v1.json`

**Interfaces:**
- Consumes: `ReleaseContext`, declared sibling `ReleaseAsset` values, and rendered public text.
- Produces: `canonical_json(value) -> bytes`, `build_provenance(context, siblings) -> bytes`, `release_provenance(context, payloads) -> bytes`, and `validate_public_tree(root, contract) -> None`.

- [ ] **Step 1: Write failing provenance serialization tests**

```python
def test_canonical_json_is_sorted_compact_utf8_lf(self):
    self.assertEqual(canonical_json({"z": 1, "a": "é"}),
                     b'{"a":"\xc3\xa9","z":1}\n')
```

Assert schema `1`, exact fields, exact gate ordering, no archive self-hash in build provenance, and five payload hashes in release provenance.

- [ ] **Step 2: Write failing allow-list and privacy-canary tests**

Inject separate canaries for ROM/BIOS, save, raw input, log, absolute/private path, IPv4/IPv6/MAC, serial/nickname, commercial evidence, token/secret, symlink, and undeclared file. Assert only a stable category such as `PRIVACY_PATH` is returned, never the sensitive value.

- [ ] **Step 3: Run both test modules and confirm failure**

Run: `python3 -m unittest tools.gba_wifi_link_release.tests.test_provenance tools.gba_wifi_link_release.tests.test_privacy -v`

- [ ] **Step 4: Implement canonical JSON and provenance builders**

```python
def canonical_json(value: object) -> bytes:
    text = json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False)
    return (text + "\n").encode("utf-8")
```

Validate lower-case 64-character SHA-256, nonnegative sizes, numeric IDs, successful required gates, and the acyclic field ownership from the spec.

- [ ] **Step 5: Implement file/field allow-list privacy validation**

Walk with `followlinks=False`, reject every non-regular declared file, compare the exact relative-name set, inspect only bounded UTF-8 public text/JSON, and scan binary inputs solely through declared name/type/hash checks.

- [ ] **Step 6: Run all provenance/privacy tests and inspect failure messages**

Run: `python3 -m unittest tools.gba_wifi_link_release.tests.test_provenance tools.gba_wifi_link_release.tests.test_privacy -v`

Expected: PASS; test output contains category names but no canary value.

- [ ] **Step 7: Commit the provenance/privacy unit**

```bash
git add packaging/gba-wifi-link/release/contract-v1.json tools/gba_wifi_link_release
git commit -m "release: add canonical private-safe provenance"
```

### Task 4: Build and verify deterministic release packages

**Files:**
- Create: `tools/gba_wifi_link_release/packager.py`
- Create: `tools/gba_wifi_link_release/verifier.py`
- Create: `tools/gba_wifi_link_release/tests/test_packager.py`
- Create: `tools/gba_wifi_link_release/tests/test_verifier.py`
- Create: `tools/gba_wifi_link_release/cli.py`
- Create: `tools/gba-wifi-link-release.py`

**Interfaces:**
- Consumes: admitted `ReleaseContext`, exact core/fixture/licence/template paths, contract-v1, and canonical gate/toolchain metadata.
- Produces: `build_release(context, inputs, output_dir) -> ReleaseSet`, `verify_release(output_dir, context) -> ReleaseSet`, and CLI subcommands `admit`, `build`, `verify`, `render-body`.

- [ ] **Step 1: Write failing package membership and normalization tests**

Assert seven public assets, eight archive members, `0644` regular-file modes, lexicographic member order, commit-derived ZIP timestamp, Unix creator, no extra fields/comments, LF text, stable JSON, and exact internal/standalone checksum scopes.

- [ ] **Step 2: Write failing unsafe input and clean-directory verifier tests**

Cover missing/extra/duplicate/renamed entries, traversal, symlink, non-regular file, wrong mode, wrong digest, appended ZIP member, stale guide, mixed manifest, self-hash, and provenance/archive cycles.

- [ ] **Step 3: Run package/verifier tests and confirm failure**

Run: `python3 -m unittest tools.gba_wifi_link_release.tests.test_packager tools.gba_wifi_link_release.tests.test_verifier -v`

- [ ] **Step 4: Implement deterministic ZIP writing**

```python
def zip_info(name: str, epoch: int) -> zipfile.ZipInfo:
    stamp = time.gmtime(epoch)[:6]
    info = zipfile.ZipInfo(name, stamp)
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    return info
```

Write members in contract order with `compresslevel=9`; reject epochs outside ZIP's representable range instead of silently clamping.

- [ ] **Step 5: Implement the acyclic build order**

Render guide/notice → build provenance → internal checksum → archive → release provenance over five payloads → standalone checksum over six preceding assets. Construct `ReleaseSet` only after `verify_release` succeeds.

- [ ] **Step 6: Implement CLI parsing with atomic output-directory creation**

Build into a sibling temporary directory created with `tempfile.mkdtemp`, verify it, then rename into a previously absent final directory. Refuse to merge with or overwrite an existing output directory.

- [ ] **Step 7: Prove two clean runs are byte-identical**

```bash
tmp_a=$(mktemp -d)
tmp_b=$(mktemp -d)
python3 tools/gba-wifi-link-release.py build --fixture synthetic --output "$tmp_a/release"
python3 tools/gba-wifi-link-release.py build --fixture synthetic --output "$tmp_b/release"
diff -r --no-dereference "$tmp_a/release" "$tmp_b/release"
```

Expected: no diff; `verify` passes independently in both directories.

- [ ] **Step 8: Run the complete release-tool suite and commit**

```bash
python3 -m unittest discover -s tools/gba_wifi_link_release/tests -p 'test_*.py' -v
git add tools/gba-wifi-link-release.py tools/gba_wifi_link_release
git commit -m "release: build deterministic Android bundles"
```

### Task 5: Implement a transactional, idempotent publisher

**Files:**
- Create: `tools/gba_wifi_link_release/github.py`
- Create: `tools/gba_wifi_link_release/publisher.py`
- Create: `tools/gba_wifi_link_release/tests/test_publisher.py`
- Create: `tools/gba_wifi_link_release/tests/fake_gh.py`
- Modify: `tools/gba_wifi_link_release/cli.py`

**Interfaces:**
- Consumes: verified `ReleaseSet`, rendered release body, `ReleaseContext`, and a `GitHubClient` implementation.
- Produces: `publish_release(client, release_set, body) -> PublishResult`, CLI subcommand `publish`, and a real `GhClient` adapter whose subprocess boundary is replaceable in tests.

- [ ] **Step 1: Define the client protocol and write the failing success-sequence test**

```python
class GitHubClient(Protocol):
    def get_release(self, tag: str) -> RemoteRelease | None: ...
    def create_draft(self, context: ReleaseContext, body: bytes) -> RemoteRelease: ...
    def upload(self, release_id: int, path: Path) -> RemoteAsset: ...
    def download_assets(self, release_id: int, output: Path) -> None: ...
    def attest(self, paths: tuple[Path, ...]) -> None: ...
    def publish(self, release_id: int) -> None: ...
    def delete_draft(self, release_id: int) -> None: ...
```

Assert call order: get → create draft → seven uploads → download/read-back → attest → publish → final get/download.

- [ ] **Step 2: Add failing fault and idempotency tests**

Cover each upload index, metadata/hash/count mismatch, attestation failure, safe draft cleanup, ambiguous publish with exact/conflicting read-back, exact public rerun with zero mutations, conflicting public state, and an unrelated pre-existing draft.

- [ ] **Step 3: Run publisher tests and confirm failure**

Run: `python3 -m unittest tools.gba_wifi_link_release.tests.test_publisher -v`

- [ ] **Step 4: Implement orchestration against the protocol**

Never catch and downgrade `ReleaseConflict`. On pre-publication failure, delete only a draft whose ID/tag/target were created by the current operation. After an ambiguous publish result, query public state and compare the entire canonical model before returning success.

- [ ] **Step 5: Implement `GhClient` with argument arrays, bounded JSON, and no shell interpolation**

```python
def _run(self, *args: str) -> bytes:
    return subprocess.run(
        [self.gh, *args, "--repo", self.repository],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=self.env,
    ).stdout
```

Use `gh api`/`gh release` with explicit repository, tag, release ID, and file paths. Parse JSON with duplicate-key rejection and bounded sizes.

- [ ] **Step 6: Add the `publish` CLI subcommand and fake executable injection**

Require an already verified release directory and `--repository Aelvryx/mgba-wifi-link`; accept `--gh-bin` only in test mode. Do not permit package building from the publish command.

- [ ] **Step 7: Run publisher and full tool suites, then commit**

```bash
python3 -m unittest tools.gba_wifi_link_release.tests.test_publisher -v
python3 -m unittest discover -s tools/gba_wifi_link_release/tests -p 'test_*.py' -v
git add tools/gba_wifi_link_release
git commit -m "release: add transactional GitHub publisher"
```

### Task 6: Make protected CI reusable and add policy tests

**Files:**
- Modify: `.github/workflows/gba-wifi-link-ci.yml`
- Create: `tools/gba_wifi_link_release/workflow_policy.py`
- Create: `tools/gba_wifi_link_release/tests/test_workflow_policy.py`
- Modify: `tools/gba_wifi_link_release/fixtures/v0.2.0-history.json`

**Interfaces:**
- Consumes: Existing six-job CI behavior and release contract.
- Produces: A reusable `workflow_call` entry that preserves `push: master` and `pull_request`, a fixed Android artifact/build-metadata handoff, and `validate_workflow_policy(repo) -> None`.

- [ ] **Step 1: Write failing source-policy tests before editing YAML**

Assert the original six job IDs/names/triggers remain, `workflow_call` exists, all jobs default to `contents: read`, the Android job uploads only the expected core/metadata, and third-party `uses:` entries on artifact/release paths are full 40-hex SHAs.

- [ ] **Step 2: Record the current normalized CI behavioral baseline**

Capture job names, CMake flags, focused targets/regex, sanitizer environments, fixture commands, NDK/toolchain versions, boundary checks, and upstream exception in the history fixture. Test that refactoring preserves this normalized baseline.

- [ ] **Step 3: Run policy tests and confirm the missing reusable contract failure**

Run: `python3 -m unittest tools.gba_wifi_link_release.tests.test_workflow_policy -v`

- [ ] **Step 4: Add `workflow_call` without weakening existing triggers**

Keep `push.branches: [master]` and `pull_request`. Add reusable inputs only for exact source/ref metadata that cannot be derived safely; do not accept arbitrary commands, build flags, runner labels, or publish permissions.

- [ ] **Step 5: Upload the inspected Android core and canonical build metadata**

Generate metadata after `file` and boundary checks. Upload only `mgba_libretro.so` plus canonical JSON using a reviewed full-SHA action; set a short bounded retention and `if-no-files-found: error`.

- [ ] **Step 6: Resolve and record full third-party action SHAs**

Resolve the selected action tags with explicit calls such as
`gh api repos/actions/checkout/git/ref/tags/v6`,
`gh api repos/actions/upload-artifact/git/ref/tags/v4`,
`gh api repos/actions/download-artifact/git/ref/tags/v5`, and
`gh api repos/actions/attest-build-provenance/git/ref/tags/v3`; peel annotated
action tags when needed. Record the resulting commit and human version in
contract/provenance tests; reject mutable `@vN` syntax on the
artifact-producing or privileged path.

- [ ] **Step 7: Run policy tests plus the six existing jobs locally where applicable**

Run the existing configure/build/test commands from `.github/workflows/gba-wifi-link-ci.yml` without changing their target counts or expected pinned exception.

- [ ] **Step 8: Commit the reusable validation unit**

```bash
git add .github/workflows/gba-wifi-link-ci.yml tools/gba_wifi_link_release
git commit -m "ci: expose protected release validation"
```

### Task 7: Add the fully automated tag release workflow

**Files:**
- Create: `.github/workflows/gba-wifi-link-release.yml`
- Modify: `tools/gba_wifi_link_release/tests/test_workflow_policy.py`
- Modify: `tools/gba_wifi_link_release/cli.py`
- Modify: `packaging/gba-wifi-link/release/contract-v1.json`

**Interfaces:**
- Consumes: reusable protected CI, Android artifact contract, release CLI, exact tag event, and contract-v1.
- Produces: A non-cancelling per-tag workflow that admits, validates, dual-builds, packages, attests, transactionally stages, verifies, and automatically publishes.

- [ ] **Step 1: Extend failing policy tests for trigger, concurrency, and permissions**

Assert tags-only `v*` trigger, no `workflow_dispatch`, concurrency keyed by `github.ref` with `cancel-in-progress: false`, top-level `contents: read`, and exactly one publisher with `contents: write`, `attestations: write`, and `id-token: write`.

- [ ] **Step 2: Add failing graph/isolation tests**

Assert validation precedes both clean builds, comparison precedes packaging, packaging precedes publisher, publisher has no checkout/build/render/download-from-network step, and every third-party action is pinned to a full commit.

- [ ] **Step 3: Create the workflow skeleton and exact-source admission job**

Use full-history checkout for tag peeling/reachability, call `gba-wifi-link-release.py admit`, export only canonical JSON/artifacts, and fail before release mutation for every admission error.

- [ ] **Step 4: Compose protected validation and two independent Android builds**

One build may consume the protected Android job output; the second must start from a fresh checkout/build directory. Compare with both `sha256sum` and `cmp --silent`; write one admitted core plus both build identities.

- [ ] **Step 5: Add deterministic package and self-verification jobs**

Run `build` in two clean directories, compare recursively, run `verify` on both, and upload one canonical release directory as the sole publisher input.

- [ ] **Step 6: Add attestation and automatic publisher job**

Download only the canonical workflow artifact, run `verify`, recheck remote tag object/commit, attest the core/archive, run `publish`, and finally re-download/verify the public seven-asset set. Do not rebuild or render in this job.

- [ ] **Step 7: Add failure fixtures and static assertions**

Test missing gate, mismatched build, tag movement, package corruption, attestation failure, conflicting release, and absence of a second human gate. Ensure no failure path invokes public publication.

- [ ] **Step 8: Run workflow/tool tests and commit**

```bash
python3 -m unittest tools.gba_wifi_link_release.tests.test_workflow_policy -v
python3 -m unittest discover -s tools/gba_wifi_link_release/tests -p 'test_*.py' -v
git add .github/workflows/gba-wifi-link-release.yml packaging/gba-wifi-link/release tools/gba_wifi_link_release
git commit -m "ci: publish releases from approved tags"
```

### Task 8: Protect release tags and document the maintainer contract

**Files:**
- Create: `.github/rulesets/gba-wifi-link-release-tags.json`
- Create: `tools/gba_wifi_link_release/tag_policy.py`
- Create: `tools/gba_wifi_link_release/tests/test_tag_policy.py`
- Create: `docs/gba-wifi-link-release.md`
- Modify: `tools/gba-wifi-link-release.py`

**Interfaces:**
- Consumes: GitHub repository ruleset JSON/API and the approved immutable-tag policy.
- Produces: `validate_tag_ruleset(actual, expected) -> None`, CLI `verify-tag-policy`, tracked exact policy JSON, and maintainer release/recovery instructions.

- [ ] **Step 1: Write failing ruleset tests**

Assert repository target, active enforcement, `refs/tags/v*` inclusion, no broad exclusion, creation allowed, update/deletion blocked, and an empty or explicitly minimal bypass list. Reject drift in any field.

- [ ] **Step 2: Run the test and confirm the missing policy failure**

Run: `python3 -m unittest tools.gba_wifi_link_release.tests.test_tag_policy -v`

- [ ] **Step 3: Add exact tracked policy JSON and validator**

Canonicalize only documented GitHub-generated IDs away; compare all semantic fields exactly and print a bounded JSON-pointer-style difference without dumping tokens or unrelated repository settings.

- [ ] **Step 4: Write maintainer instructions**

Document: prepare exact-version notes, confirm the runtime qualification
decision, set `release_tag` to the reviewed concrete version, set
`release_commit="$(git rev-parse origin/master)"`, create the annotated tag with
`git tag -a "$release_tag" -m "GBA Wi-Fi Link $release_tag" "$release_commit"`,
push that tag, and then observe automation only. Explain failure, rerun,
immutable correction, and no production tag during this implementation.

- [ ] **Step 5: Add read-only live-policy verification to the CLI and CI policy job**

Use `gh api repos/Aelvryx/mgba-wifi-link/rulesets`; select by exact tracked name and target, validate it, and fail on missing/duplicate/drifted state.

- [ ] **Step 6: Run tag-policy tests and commit before applying external settings**

```bash
python3 -m unittest tools.gba_wifi_link_release.tests.test_tag_policy -v
git add .github/rulesets docs/gba-wifi-link-release.md tools/gba-wifi-link-release.py tools/gba_wifi_link_release
git commit -m "release: define immutable tag policy"
```

Do not mutate the live ruleset until focused review approves this commit.

### Task 9: Align product guidance and roadmap with maintainable-alpha policy

**Files:**
- Modify: `README.md`
- Modify: `ROADMAP.md`
- Modify: `SUPPORT.md`
- Modify: `.github/ISSUE_TEMPLATE/bug.yml`
- Modify: `.github/ISSUE_TEMPLATE/compatibility.yml`
- Modify: `packaging/gba-wifi-link/release/templates/INSTALL-AND-USAGE.md.in`
- Modify: `tools/gba-wifi-link-boundary-policy.json`
- Modify: `tools/test-audit-gba-wifi-link-boundary.py`
- Modify: `tools/audit-gba-wifi-link-boundary.py`

**Interfaces:**
- Consumes: Fully automated immutable release contract and neutral-feedback decision.
- Produces: Current public/maintainer wording, updated roadmap/milestone direction, and permanent boundary checks.

- [ ] **Step 1: Add failing documentation/boundary policy fixtures**

Require “maintainable alpha,” issue #21 before #22/#23, no issue #20 release gate, no request for reports, no prohibition on feedback, no support promise, and no statement that release publication requires a manual Publish action.

- [ ] **Step 2: Run boundary tests and confirm the old supportable/feedback wording fails**

Run: `python3 tools/test-audit-gba-wifi-link-boundary.py`

- [ ] **Step 3: Update README, roadmap, support, and issue templates**

Keep issue channels available but passive. Change v0.2.1 to “Maintainable alpha,” make #21/#22/#23 its committed outcomes, defer #20 outside the exit gate, and retain honest alpha/upstream/privacy limits.

- [ ] **Step 4: Update current release guidance and permanent audit policy**

Describe future tag automation separately from historical v0.2.0 correction. Allow protocol-v2 terminology only in technical compatibility contexts and retain canonical GBA Wi-Fi Link product names.

- [ ] **Step 5: Run docs/boundary tests and search for contradictions**

```bash
python3 tools/test-audit-gba-wifi-link-boundary.py
python3 tools/audit-gba-wifi-link-boundary.py
rg -n "supportable alpha|issue #20.*gate|solicit.*feedback|manual.*Publish" README.md ROADMAP.md SUPPORT.md .github packaging/gba-wifi-link/release docs
```

Expected: only explicitly historical/specification discussions remain.

- [ ] **Step 6: Commit the current guidance unit**

```bash
git add README.md ROADMAP.md SUPPORT.md .github/ISSUE_TEMPLATE packaging/gba-wifi-link/release tools/gba-wifi-link-boundary-policy.json tools/audit-gba-wifi-link-boundary.py tools/test-audit-gba-wifi-link-boundary.py
git commit -m "docs: align maintainable alpha release policy"
```

### Task 10: Run complete local validation and open the working draft PR

**Files:**
- Modify: `.github/workflows/gba-wifi-link-ci.yml` (add release-tool tests to fixture job)
- Modify: `openspec/changes/automate-release-artifacts-provenance/tasks.md`
- Create later: `openspec/changes/automate-release-artifacts-provenance/verify.md`

**Interfaces:**
- Consumes: Tasks 1–9 implementation.
- Produces: One reviewable exact head, one working draft PR, protected evidence, and no runtime/production release mutation.

- [ ] **Step 1: Add all release-tool suites to the fixture/reproducibility job**

Run `python3 -m unittest discover -s tools/gba_wifi_link_release/tests -p 'test_*.py' -v` and two-clean-directory synthetic package comparison in protected CI.

- [ ] **Step 2: Run local release/tooling verification**

```bash
python3 -m unittest discover -s tools/gba_wifi_link_release/tests -p 'test_*.py' -v
python3 tools/test-audit-gba-wifi-link-boundary.py
python3 tools/audit-gba-wifi-link-boundary.py
openspec validate automate-release-artifacts-provenance --strict
git diff --check
```

- [ ] **Step 3: Run focused normal and sanitizer suites**

Execute the exact normal, ASan/UBSan with leak detection, and TSan commands from `.github/workflows/gba-wifi-link-ci.yml`; require every applicable focused executable to pass.

- [ ] **Step 4: Run complete suite, fixtures, helpers, and dual Android builds**

Run the complete applicable suite with the separately checked pinned upstream exception, byte-identical fixture rebuild, analyzer/helper tests, and two clean NDK `27.2.12479018` Android builds compared by `sha256sum` and `cmp`.

- [ ] **Step 5: Prove no runtime source changed**

```bash
git diff --name-only "$(git merge-base HEAD origin/master)"..HEAD -- include src \
  ':!src/platform/libretro/CMakeLists.txt'
```

Expected: no GBA/runtime implementation files. If runtime behavior changed, stop and define proportionate physical qualification before tagging or landing.

- [ ] **Step 6: Commit final local corrections and open one draft PR**

```bash
git add .github/workflows/gba-wifi-link-ci.yml openspec/changes/automate-release-artifacts-provenance
git commit -m "test: gate automated release workflow"
git push -u origin agent/automate-release-artifacts-provenance
gh pr create --draft --repo Aelvryx/mgba-wifi-link --base master --head agent/automate-release-artifacts-provenance
```

- [ ] **Step 7: Wait for all protected checks on the exact PR head**

Run: `pr_number="$(gh pr view --repo Aelvryx/mgba-wifi-link --json number --jq .number)"; gh pr checks --watch --repo Aelvryx/mgba-wifi-link "$pr_number"`

Record run/job IDs and exact head SHA; keep the PR draft.

### Task 11: Review and rehearse the real remote transaction in isolation

**Files:**
- Create: `docs/automated-release-rehearsal.md`
- Modify: `.github/rulesets/gba-wifi-link-release-tags.json` only if review proves a policy defect
- Modify: release implementation/tests only for reviewed findings

**Interfaces:**
- Consumes: Green exact-head draft PR, reviewed ruleset/publisher, full GitHub authorization.
- Produces: Live tag ruleset verification, private disposable end-to-end evidence, independent review, and complete cleanup.

- [ ] **Step 1: Obtain focused independent review before external mutation**

Review exact packager bytes, checksum/provenance DAG, privacy canaries, workflow graph, action pins, privilege separation, tag ruleset, publisher failure semantics, and protected run. Address Critical/Important findings before continuing.

- [ ] **Step 2: Apply and read back the production `v*` tag ruleset**

Resolve the exact repository/ruleset target read-only, create or update only the named tracked ruleset through `gh api`, then run `gba-wifi-link-release.py verify-tag-policy`. Do not create a production release tag.

- [ ] **Step 3: Create an explicitly scoped private disposable repository**

```bash
rehearsal_run_id="$(gh run list --repo Aelvryx/mgba-wifi-link --branch agent/automate-release-artifacts-provenance --limit 1 --json databaseId --jq '.[0].databaseId')"
rehearsal_repo="Aelvryx/mgba-wifi-link-release-rehearsal-${rehearsal_run_id}"
gh repo create "$rehearsal_repo" --private --disable-issues --disable-wiki
```

Require `rehearsal_run_id` to be a non-empty decimal value, then read back
owner/name/visibility and refuse any target not matching `rehearsal_repo`
exactly.

- [ ] **Step 4: Exercise successful automatic publication and independent download**

Push only reviewed synthetic/re-distributable rehearsal content and an annotated synthetic version tag. Let the workflow publish automatically. Download all seven assets into a fresh directory; verify external/internal manifests, archive members, build/release provenance, attestations, tag object/commit, body, target, and prerelease state.

- [ ] **Step 5: Exercise idempotent rerun and isolated failures**

Rerun the same tag and prove zero mutations. Use the fake/isolated controls to exercise partial upload, conflicting metadata/hash, ambiguous publish response, failed attestation, and draft cleanup. Attempt tag update/deletion only in the disposable repository and confirm its rules prevent them where configured.

- [ ] **Step 6: Record bounded evidence and audit privacy**

Write only repository/run IDs, public synthetic hashes, state transitions, conclusions, and cleanup identifiers to `docs/automated-release-rehearsal.md`. Scan the evidence and Actions artifacts for every prohibited canary category.

- [ ] **Step 7: Delete the exact disposable target and verify absence**

```bash
gh repo delete "$rehearsal_repo" --yes
gh repo view "$rehearsal_repo"
```

Expected: deletion succeeds; subsequent view fails. Record that the rehearsal release/tag/repository were destroyed and are unrecoverable.

- [ ] **Step 8: Address final review findings and rerun affected evidence**

Repeat only tests/rehearsal paths whose semantics changed. Push the correction to the same draft PR and require all six protected jobs on the new exact head.

- [ ] **Step 9: Commit rehearsal evidence**

```bash
git add docs/automated-release-rehearsal.md .github tools packaging openspec/changes/automate-release-artifacts-provenance
git commit -m "docs: record automated release rehearsal"
git push
```

### Task 12: Verify, archive, land, and close issue #21

**Files:**
- Create: `openspec/changes/automate-release-artifacts-provenance/verify.md`
- Create: `openspec/changes/automate-release-artifacts-provenance/retrospective.md`
- Create through sync: `openspec/specs/automated-release-provenance/spec.md`
- Modify through sync: `openspec/specs/public-alpha-distribution/spec.md`
- Archive: `openspec/changes/archive/$(date -u +%F)-automate-release-artifacts-provenance/`

**Interfaces:**
- Consumes: Complete task ledger, independent review, exact protected evidence, ruleset read-back, and rehearsal record.
- Produces: Verified/archived authoritative capabilities, merged protected master, issue #21 closure, and an enabled workflow awaiting a future approved production tag.

- [ ] **Step 1: Complete formal verification**

Use `$openspec-verify-change`; map every requirement/scenario to its test, workflow evidence, policy read-back, or rehearsal result. Explicitly record the no-runtime-diff proof and why no device gameplay was manufactured.

- [ ] **Step 2: Complete the retrospective**

Record dual-build reproducibility, remote transaction outcomes, destructive cleanup, workflow/schema friction, review findings, remaining runner/signing boundaries, and non-blocking follow-ups.

- [ ] **Step 3: Sync both capability deltas**

Use `$openspec-sync-specs`; verify the new `automated-release-provenance` spec and renamed/modified neutral-feedback requirements under `public-alpha-distribution` are exact.

- [ ] **Step 4: Archive the change and update path-based audit policy**

Use `$openspec-archive-change`, classify the archive as historical where required, and run:

```bash
openspec validate --all --strict
python3 tools/test-audit-gba-wifi-link-boundary.py
python3 tools/audit-gba-wifi-link-boundary.py
git diff --check
```

- [ ] **Step 5: Push the immutable archive head and wait for exact-head checks**

Keep the same PR draft. Require the six protected jobs and release-tool policy tests to pass on the archive head; obtain final review only for any post-review semantic change.

- [ ] **Step 6: Verify production safety before readiness**

Confirm the real `v*` trigger and tag ruleset are enabled, no production version tag/release was created, v0.2.0 is unchanged, and no disposable rehearsal repository/tag/release remains.

- [ ] **Step 7: Make the existing PR ready and merge last**

```bash
pr_number="$(gh pr view --repo Aelvryx/mgba-wifi-link --json number --jq .number)"
gh pr ready --repo Aelvryx/mgba-wifi-link "$pr_number"
gh pr merge --repo Aelvryx/mgba-wifi-link "$pr_number" --squash --delete-branch
```

- [ ] **Step 8: Verify merged master and close issue #21**

Fetch `origin/master`, confirm the merge commit/tree, rerun read-only schema/ruleset checks, and close issue #21 with the merged commit, protected run, rehearsal evidence, and the statement that the next approved annotated tag will publish fully automatically.

- [ ] **Step 9: Commit-point note**

No commit follows the protected merge. Any later release notes and production version tag belong to that release's own reviewed preparation, not this implementation change.
