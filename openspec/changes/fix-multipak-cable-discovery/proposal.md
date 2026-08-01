## Why

The replicated-pair runtime has proven full-speed generic MULTI transfers in diagnostic workloads and commercial games, but Zelda: Four Swords was reported to stall during cable discovery before the final topology-settled alpha.2 build had been retested. This change first retests that exact baseline; it only authorizes causal diagnosis if the failure remains and never pre-authorizes an unknown production behavior change.

## What Changes

- Retest the topology-settled alpha.2 build before adding diagnostic infrastructure or changing behavior.
- If the failure remains, add bounded, opt-in tracing for guest SIOCNT/RCNT observations, attempted writes, SIO mode and line changes, player identity, START edges, coordinator state, and transfer completion.
- Diagnose identical initial states, configuration, and private per-frame inputs in strict widening order: ordinary local mGBA lockstep, a network-free `GBAReplicatedPair`, then the full protocol-v2 runtime.
- Produce an evidence report naming the first causal happens-before, state, or characterized timing difference.
- Stop after diagnosis and amend the proposal, design, specification, and tasks with the affected layer's exact generic invariant before any production behavior change; obtain focused review of that delta.
- After that review only, add a redistributable automated regression, implement the approved correction, and qualify linking plus brief two-player gameplay on two Android devices.
- Keep complex navigation and RetroArch's app-private core-install confirmation human-owned; keep builds, staged-artifact verification, deterministic replay, trace extraction, comparison, automated runtime validation, teardown, and cleanup agent-owned.

## Capabilities

### New Capabilities

- `gba-multipak-cable-discovery`: A staged Multi-Pak discovery investigation using Four Swords as the qualification title, with deterministic cross-layer evidence and a mandatory reviewed requirement before any generic correction.

### Modified Capabilities

None during Stage A. Once diagnosis identifies the affected existing behavior, Stage B SHALL add a precise delta requirement for that capability and obtain focused review. Protocol v2 remains experimental and unfrozen; this package does not pre-authorize common SIO, lockstep, replica, pair, adapter, or wire changes.

## Impact

- Stage A affects only qualification material and, if the baseline still fails, diagnostic observer/export/comparison infrastructure.
- A later production correction may affect common GBA SIO and local lockstep code, replicated-pair construction/scheduling, or the libretro protocol-v2 adapter only after an evidence-backed specification revision.
- Conditional diagnostic trace structures and tests have explicit read-coalescing, ordering, memory, volume, concurrency, and lifecycle bounds.
- Stage B adds a non-commercial fixture or deterministic replay reproducing the reviewed generic transition.
- Updates compatibility and validation documentation with exact source, binary, trace, and device evidence.
- Does not add production dependencies, redistribute Four Swords content, or expand the two-player Multi-Pak alpha scope.
