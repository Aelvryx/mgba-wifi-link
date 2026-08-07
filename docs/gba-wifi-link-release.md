# Automated GBA Wi-Fi Link releases

This repository publishes a future GBA Wi-Fi Link Android release from one
deliberate maintainer action: pushing an approved annotated `vMAJOR.MINOR.PATCH`
tag. The tag-triggered workflow performs the protected validation, two clean
Android builds, byte comparison, deterministic packaging, provenance
attestation, draft staging, remote verification, and publication. There is no
second approval, dispatch, or **Publish** click.

This document describes future releases. It does not create a production tag or
alter the existing `v0.2.0` prerelease.

## Before the tag

1. Make the release decision in the normal reviewed work, including the
   proportionate runtime-qualification decision for the exact candidate.
2. Commit reviewed notes at
   `packaging/gba-wifi-link/releases/vMAJOR.MINOR.PATCH/RELEASE-NOTES.md`.
   They must be specific to the intended version and contain no placeholders.
3. Confirm `master` has passed its protected checks and that the intended source
   is exactly the current protected `origin/master` commit.
4. Fetch the protected history, then use the reviewed concrete version in these
   commands:

   ```sh
   git fetch origin master --tags
   release_tag=vMAJOR.MINOR.PATCH
   release_commit="$(git rev-parse origin/master)"
   git tag -a "$release_tag" -m "GBA Wi-Fi Link $release_tag" "$release_commit"
   git push origin "$release_tag"
   ```

Pushing that annotated tag is the entire production release action. Observe the
tag workflow; do not upload replacement assets, edit the release, or manually
publish a draft.

## Failure and rerun

Automation publishes only after every required check, independent build,
package verification, tag recheck, and attestation succeeds. A failure leaves
evidence in the workflow and does not turn an uncertain artifact into a public
release.

The `v*` ruleset permits a new tag to be created but blocks changing or deleting
it afterwards. Never force-push, retarget, or delete a release tag. If a
published release is wrong, retain the immutable evidence and prepare a new,
reviewed version and annotated tag. If an exact run is retried after publication,
automation verifies the existing release read-only and succeeds only when every
asset and identity still matches.

If an unpublished run fails, inspect the workflow first. Correct the reviewed
source or notes on `master`, wait for protected validation, then use a new
version tag. Do not treat rerunning a failed tag or modifying remote state as a
correction path.

## Recovery and policy checks

The tracked policy is
`.github/rulesets/gba-wifi-link-release-tags.json`. GitHub's ruleset list API
returns summaries; the complete ruleset endpoint exposes `bypass_actors` only
to a caller with write access to that ruleset. See GitHub's [ruleset REST
documentation](https://docs.github.com/en/rest/repos/rules?apiVersion=2022-11-28).

Create the `gba-wifi-link-release-governance` GitHub Environment with deployment
branches restricted to protected `master` only (no tag pattern and no manual
reviewer gate), then store `GBA_WIFI_LINK_RULESET_AUDIT_TOKEN` as an
**environment secret** there. The credential must be repository-limited and
have ruleset visibility. It may need write-capable ruleset access solely because
GitHub withholds the field from read-only callers. The audit code uses it for two
`GET` requests only, never prints it, and rejects missing or withheld
`bypass_actors` fail-closed. Do not use a broader personal credential when a
repository-limited credential is available.
GitHub documents that environment secrets are available only to jobs referencing
the environment and only after its protection rules pass; see [deployment
environments](https://docs.github.com/en/actions/reference/workflows-and-actions/deployments-and-environments).

The `GBA Wi-Fi Link release governance` workflow runs only from protected
`master` and on its schedule. It owns that credential and checks the live policy
there. The tag-triggered release workflow deliberately never receives it: a
workflow triggered by a tag is evaluated from that tag's source and must not be
able to rewrite a secret-bearing audit step. A master-only environment secret
also prevents a tag workflow from referencing the credential directly. The tag
ruleset itself remains the release precondition; the governance audit
continuously detects policy drift.

For a local, read-only audit, provide the same credential through the
environment rather than placing it in a command line or log:

```sh
export GBA_WIFI_LINK_RULESET_AUDIT_TOKEN='…'
python3 tools/gba-wifi-link-release.py verify-tag-policy
```

That command first selects exactly one canonical summary by name and repository,
then reads and validates its complete detail by ID. It requires an active tag
ruleset on `Aelvryx/mgba-wifi-link`, covering `refs/tags/v*`, with no bypass
actors and with deletion and force-update protection. During implementation or
rehearsal, do not create a real version tag or publish a release; only the
dedicated, reviewed release workflow and a later approved tag perform
publication.
