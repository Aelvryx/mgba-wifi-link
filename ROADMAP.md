# Roadmap

GBA Wi-Fi Link exists to make multiplayer on real handhelds feel like a local
GBA cable session while using stock RetroArch and remaining a reviewable mGBA
fork. This roadmap describes product outcomes and the evidence that unlocks
them. It is directional rather than date-driven.

GitHub issues carry bounded work. OpenSpec is used when a change needs a new
behavioural or architectural contract. Compatibility reports remain a
continuous evidence stream rather than an arbitrary release quota.

## North star

The long-term product is a dependable, understandable way to play GBA
multiplayer across modern handhelds without game-specific hacks, commercial
content, a private frontend fork, or network latency on every emulated cable
word.

Every expansion must preserve four things:

1. Generic hardware behaviour rather than title detection.
2. Deterministic corresponding replicas and authoritative player input.
3. Local save ownership with transactional failure recovery.
4. Evidence that is automated wherever practical and proportionate where a
   human must play the game.

## Current product: v0.2.0 usable alpha

| Dimension | Current position |
| --- | --- |
| Players | Two physical devices, one authoritative player each |
| Cartridge model | Identical-ROM Multi-Pak |
| Cable model | GBA `MULTI` mode through a replicated local P0/P1 pair |
| Frontend | Stock RetroArch Netpacket |
| Published platform | Android ARM64/API 21+ |
| Network | Trusted local LAN or direct Android hotspot |
| Latency | Calibrated fixed delay; qualified Stable policy has a two-frame floor |
| Persistence | Local authoritative saves; verified state/save rollback on uncertain teardown |
| Qualified games | Mario Kart: Super Circuit and Zelda: Four Swords; Advance Wars user playtest |

The current product deliberately does not promise universal Multi-Pak
compatibility, Single-Pak, four players, Wireless Adapter/RFU, reconnection,
internet relay, or live-session savestates.

## Direction

```text
v0.2.0: working public alpha
          |
          v
v0.2.1: Maintainable alpha
  release automation + upstream refresh + parser fuzzing
          |
          v
portable and resilient operation
  resource budgets + Android lifecycle + H700 feasibility
          |
          v
evidence-backed capability choice
  Four Player Multi-Pak  OR  Single-Pak multiboot
          |
          v
broader GBA multiplayer
  extra cable modes + sensors + RFU + resilience + latency research
```

The arrows are dependency and confidence gates, not promises that every item
must wait for all earlier work. Research can proceed in parallel when it is
bounded and does not pre-authorize production behaviour.

## Now: v0.2.1 Maintainable alpha

The next release should make the working alpha easier to distribute,
diagnose, maintain, and update. It intentionally adds no new emulation mode.

### Committed outcomes

1. [Automated release artifacts and provenance (#21)](https://github.com/Aelvryx/mgba-wifi-link/issues/21)
   turn an approved annotated tag into a repeatable Android bundle with verified
   identity and checksums, then publish it fully automatically.
2. [Current upstream mGBA refresh (#22)](https://github.com/Aelvryx/mgba-wifi-link/issues/22)
   rebases the patch stack onto a reviewed pin and proves that the product and
   versioned compatibility contract survive.
3. [Network-controlled decoder fuzzing (#23)](https://github.com/Aelvryx/mgba-wifi-link/issues/23)
   covers packet, profile, calibration, replica-manifest, and copied-transport
   boundaries under sanitizers.

The maintainable-alpha release commitment is issues #21, #22, and #23. Issue
#20 remains useful deferred diagnostic work. Issue #20 is not a v0.2.1 exit
gate.

[Compatibility evidence (#8)](https://github.com/Aelvryx/mgba-wifi-link/issues/8)
continues alongside this milestone. A confirmed success or failure improves
the matrix, but v0.2.1 is not held hostage to an arbitrary number of commercial
playthroughs.

### Exit gate

v0.2.1 is ready when issues #21, #22, and #23 are complete, the protected
automated suite is green on the release candidate, the published bundle is
reproducible and correctly identified, and any physical smoke is proportionate
to actual runtime changes. Documentation-only or tooling-only work does not
manufacture a need for another long commercial playtest.

## Next: portable and resilient operation

This horizon establishes whether the current architecture can travel beyond
the already-qualified Android devices and remain well-behaved when mobile
platforms interrupt it.

1. [Establish resource budgets (#25)](https://github.com/Aelvryx/mgba-wifi-link/issues/25)
   for CPU, memory, frame time, audio, temperature, verification, and replica
   cost. These budgets are decision inputs, not retrospective pass marks.
2. [Qualify H700 Linux handhelds (#11)](https://github.com/Aelvryx/mgba-wifi-link/issues/11)
   only if their frontend exposes the required Netpacket API and their measured
   resource envelope is credible. A documented infeasibility result is valid.
3. [Harden Android lifecycle teardown (#24)](https://github.com/Aelvryx/mgba-wifi-link/issues/24)
   across backgrounding, sleep, frontend stop, and network changes without
   silently expanding into transparent reconnection.

This horizon earns a release milestone only after the first measurements show
which outcomes belong together. We will not promise a platform before proving
its frontend and hardware can support the product.

## Discovery: choose the next multiplayer capability

Two valuable expansions are known, but implementing both together would mix
two different architectures and weaken reviewability.

| Candidate | User value | Must prove before implementation |
| --- | --- | --- |
| [Four Player Multi-Pak (#10)](https://github.com/Aelvryx/mgba-wifi-link/issues/10) | Recreates the full cable player count for supported titles | Frontend peer topology, authoritative input ownership, four-replica CPU/memory/thermal cost, save ownership, verification, and partial-disconnect semantics |
| [Single-Pak multiboot (#26)](https://github.com/Aelvryx/mgba-wifi-link/issues/26) | Opens games whose multiplayer client is downloaded from one cartridge | BIOS/multiboot accuracy, asymmetric content and boot identity, WRAM client state, persistence, retry/failure behaviour, and a redistributable fixture |

Each investigation ends with a bounded feasibility report, rough size,
dependency map, regression strategy, and explicit proceed/defer recommendation.
Only the selected capability receives an OpenSpec implementation proposal and
release commitment. Novelty alone does not select it; user reach, feasibility,
resource headroom, and ability to test it generically do.

## Later: broaden the GBA link ecosystem

These are legitimate directions, not commitments. They remain here until a
concrete need and a credible first increment exist.

### Cable and cartridge fidelity

- Synchronize tilt, gyro, solar/luminance, and other cartridge-owned inputs
  instead of rejecting affected cartridges.
- Characterize NORMAL8/NORMAL32 and cross-ROM cable sessions before relaxing
  exact-ROM equality.
- Treat Wireless Adapter/RFU as a distinct peripheral and multiplayer protocol,
  not another cable mode.
- Keep GB/GBC link outside the present GBA product boundary unless it receives
  its own architecture.

### Latency and resilience

- Prediction or rollback may reduce perceived latency, but it must define
  presentation, audio, save, verification, and failure semantics together.
- Reconnection, host migration, and live-session savestates require a complete
  transferable session model rather than merely retaining a socket.
- Internet relay or NAT traversal requires a hostile-peer threat model and
  should reuse frontend transport facilities rather than adding silent direct
  sockets.

## Product principles

1. Correct generic cable behaviour before game-specific workarounds.
2. Keep network latency out of individual emulated cable transactions.
3. Fail closed when inputs or deterministic policy cannot be synchronized.
4. Preserve local save ownership and transactional rollback.
5. Keep human input for complex navigation and gameplay; automate mechanical
   building, replay, monitoring, evidence capture, and analysis.
6. Let validation serve product decisions rather than become ceremony.
7. Keep the fork close enough to upstream mGBA that its patch stack remains
   reviewable and refreshable.
8. Prefer one finished capability over several half-supported modes.

## Prioritization

WSJF is a lightweight tie-breaker inside a horizon, not the product strategy.
Continuous evidence, prerequisite research, and implementation work are not
directly interchangeable merely because they can all be assigned numbers.

```text
WSJF = (user/release value + urgency + risk reduction/opportunity) / job size
```

Relative estimates use `1, 2, 3, 5, 8, 13, 21` and are reconsidered only when
new evidence changes value, dependency, or size.

| Outcome | Value | Urgency | Risk / enablement | Size | WSJF | Horizon |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Automated release provenance (#21) | 5 | 5 | 8 | 5 | **3.6** | Now |
| Upstream mGBA refresh (#22) | 3 | 5 | 8 | 5 | **3.2** | Now |
| Decoder fuzzing (#23) | 3 | 5 | 8 | 5 | **3.2** | Now |
| Resource budgets (#25) | 5 | 3 | 8 | 5 | **3.2** | Next / prerequisite |
| H700 feasibility (#11) | 5 | 3 | 5 | 5 | **2.6** | Next |
| Android lifecycle hardening (#24) | 5 | 3 | 5 | 8 | **1.6** | Next |
| Four Player feasibility (#10) | 8 | 3 | 8 | 13 | **1.5** | Discovery |
| Single-Pak feasibility (#26) | 8 | 3 | 8 | 21 | **0.9** | Discovery |

Compatibility (#8) is deliberately absent from the ranking because it is a
time-boxed continuous stream, not a finishable outcome.

## Success measures

| Concern | Evidence that matters |
| --- | --- |
| Correctness | Matching replicas and verification, bounded failure, transactional state/save restoration, no input loss |
| Playability | Sustained frame/audio delivery and bounded input rendezvous on explicitly qualified devices and paths |
| Supportability | A privacy-safe report can explain build, attachment, policy, performance, and teardown without private content |
| Maintainability | Explicit upstream pin, reviewable patch stack, reproducible releases, stable fixtures, and fuzzed peer-controlled parsers |
| Compatibility | Honest per-title evidence with verified, user-reported, failed, and untested states—not a universal claim or game-count target |

## Decision log

| Decision | Current position | Revisit when |
| --- | --- | --- |
| Production runtime | GBA Wi-Fi Link façade over the version-2 replicated session and wire contract | A reviewed architecture demonstrates a concrete incompatibility or new requirement |
| Default delay | Calibrated Stable policy with a minimum two frames | An exact candidate passes the bilateral one-frame publication gate |
| Network path | Trusted local LAN and direct Android hotspot; neither guarantees lower latency | A frontend-supported transport path and threat model are proposed |
| Players | Exactly two | Four Player feasibility and resource gates pass |
| Cartridge model | Identical Multi-Pak cartridges | Single-Pak or cross-ROM design defines asymmetric identity and persistence |
| Serial modes | GBA `MULTI` for networked play | A NORMAL-mode need is characterized generically |
| Savestates | Rejected during a live session | Complete network-session serialization is designed |
| Major capability after hardening | Undecided between Four Player and Single-Pak | Both feasibility reports make value, cost, and risk comparable |
| Upstream maintenance | Make an explicit base decision before each capability release; refresh now for v0.2.1 | Upstream fixes, security, compatibility, or accumulated drift justify an earlier refresh |

## How work is tracked

- The `v0.2.0 — Usable alpha` milestone is closed and records the first public
  release.
- The `v0.2.1 — Maintainable alpha` milestone contains issues #21–#23. Issue
  #20 is deferred and does not gate that release.
- Compatibility issue #8 remains open and unmilestoned as continuous evidence.
- `roadmap: now`, `roadmap: next`, and `roadmap: later` describe horizons, not
  promises or calendar dates.
- GitHub issues own bounded outcomes. OpenSpec owns reviewed behavioural or
  architectural changes and is archived after merge.
- This file changes when evidence or product direction changes, not after every
  task checkbox.
