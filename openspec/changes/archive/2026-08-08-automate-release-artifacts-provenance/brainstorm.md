# Automated release artifacts and provenance — brainstorm capture

## Background

The v0.2.0 public alpha proved the release contents, but assembling and correcting
them manually was tedious and error-prone. The requested outcome is simple:

> An authorized maintainer pushes one approved annotated version tag; everything
> else needed to validate, build, package, and publish the release is automatic.

This is a personal project maintained by its repository owner. A tag deliberately
pushed by that maintainer is trusted. Defending the repository from a malicious
tag authored by its own maintainer is not a project requirement.

## Selected approach

One GitHub Actions workflow triggered by canonical `vMAJOR.MINOR.PATCH` tags will:

1. validate the annotated tag, version, source commit, and tracked release notes;
2. run the existing protected test matrix against that commit;
3. produce two clean Android ARM64 builds and require byte-identical cores;
4. verify the executable identity;
5. build deterministic release assets, provenance, and checksum manifests;
6. enforce the existing public privacy boundary;
7. stage the exact asset set in a private GitHub draft;
8. read the remote state back and publish automatically only when it matches; and
9. treat an exact already-public release as read-only success while rejecting
   conflicts.

The workflow may use narrowly scoped job permissions and ordinary fail-closed
checks against accidental mistakes or failed infrastructure. Those controls do
not pretend to defend the project from an authorized maintainer intentionally
rewriting their own workflow.

## Retained quality controls

- Two independent clean builds must match byte-for-byte.
- Archive ordering, timestamps, modes, serialization, and checksum scopes are
  deterministic.
- Public assets and provenance use explicit file/field allow-lists.
- Raw ROMs, BIOS data, saves, inputs, endpoint logs, device identities, private
  paths/addresses, and secrets remain prohibited.
- A partial or conflicting public release is never silently overwritten.
- `v0.x` remains prerelease-classified.
- Runtime-sensitive changes complete their existing qualification before the
  maintainer approves the tag; release tooling does not invent a new playtest.

## Explicit non-goals

- No protected-controller or hostile-tag threat model.
- No governance credential, environment-secret ruleset audit, or tag-policy
  enforcement service.
- No disposable public rehearsal repository.
- No signed artifact-attestation requirement; the generated provenance and
  checksums are the requested provenance boundary.
- No automatic version selection, stable promotion, additional platforms,
  feedback programme, support commitment, or runtime behavior change.

## Success criterion

After this change lands, the maintainer prepares reviewed release notes and pushes
one annotated tag on the intended `master` history. A complete prerelease appears
automatically if all existing tests, both builds, package checks, and remote
read-back checks pass. Otherwise the workflow fails without creating a partial
public release.
