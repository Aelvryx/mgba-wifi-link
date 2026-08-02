## REMOVED Requirements

### Requirement: Replicated protocol v2 is the sole shipped runtime
**Reason**: The selection-era capability is replaced by the positive `gba-wifi-link-runtime` capability now that no alternate runtime exists.

**Migration**: Canonical runtime, stale-option, disconnected-execution, and versioned-wire requirements move to `gba-wifi-link-runtime`; the removal history remains in archived OpenSpec and `docs/protocol-v1-retirement.md`.

### Requirement: Retired protocol v1 cannot execute or negotiate
**Reason**: Protocol-v1 removal is a completed historical change rather than a standalone current product capability.

**Migration**: Legacy-byte rejection and no-fallback behavior remain positive requirements of `gba-wifi-link-runtime` and codec/session tests; the exhaustive removal inventory remains archived.

### Requirement: Shared invariants survive independently of v1
**Reason**: The migration from v1-owned tests is complete and permanent regression ownership no longer depends on a deleted implementation.

**Migration**: Current test ownership and behavior-neutral integration gates move to `gba-wifi-link-runtime`; the case-by-case retirement table remains historical evidence.

### Requirement: Historical evidence is preserved without live instructions
**Reason**: Current-versus-historical documentation is a continuing product rule, but it no longer belongs to a runtime-selection capability.

**Migration**: The continuing rule moves to `gba-wifi-link-runtime`; retired implementation details and audit results remain in the dated removal archive.
