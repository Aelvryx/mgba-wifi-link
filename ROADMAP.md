# Roadmap

This roadmap keeps the project pointed in a useful direction without pretending
that experimental emulator work has predictable dates. GitHub issues carry
actionable work; OpenSpec is used only when a change needs a behavioural or
architectural contract.

## Product principles

1. Correct generic cable behaviour before game-specific workarounds.
2. Keep network latency out of individual emulated cable transactions.
3. Fail closed when inputs or deterministic policy cannot be synchronized.
4. Preserve local save ownership and transactional rollback.
5. Measure real devices, but let evidence serve the product rather than turn
   qualification into ceremony.
6. Keep human input for complex navigation and gameplay; automate everything
   mechanical and repeatable.
7. Prefer a smaller dependable product over many half-supported modes.

## Lightweight WSJF

Priorities use a deliberately rough Weighted Shortest Job First calculation:

```text
WSJF = (user/release value + time criticality + risk reduction/opportunity) / job size
```

Each input uses relative buckets `1, 2, 3, 5, 8, 13, 21`, with scores rounded
half up to one decimal place. A higher score means
“consider sooner,” not “promise a date.” Scores are estimates, and dependencies
override the arithmetic: a release cannot precede the cleanup or decision that
defines what it contains. We should rescore when evidence changes, not after
every completed task.

| Rank | Outcome | Value | Urgency | Risk / enablement | Size | WSJF | Horizon |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |
| — | [Remove shipped cable-sync v1](https://github.com/Aelvryx/mgba-wifi-link/issues/6) | 5 | 8 | 8 | 3 | **7.0** | **Completed in PR #14** |
| — | [Publish the GBA Wi-Fi Link alpha](https://github.com/Aelvryx/mgba-wifi-link/issues/7) | 8 | 8 | 5 | 3 | **7.0** | **Completed in v0.2.0** |
| — | [Test hotspot latency and decide the default](https://github.com/Aelvryx/mgba-wifi-link/issues/9) | 5 | 5 | 8 | 3 | **6.0** | **Completed; retain Stable default** |
| 1 | [Grow the compatibility matrix](https://github.com/Aelvryx/mgba-wifi-link/issues/8) | 5 | 5 | 5 | 3 | **5.0** | Now / continuous |
| 2 | Produce a one-command sanitized diagnostic bundle | 5 | 3 | 8 | 5 | **3.2** | Next |
| 3 | Automate reviewed release artifacts and provenance | 5 | 3 | 8 | 5 | **3.2** | Next |
| 4 | Fuzz packet, profile, and replica-bundle decoding | 3 | 3 | 8 | 5 | **2.8** | Next |
| 5 | [Qualify H700 Linux handhelds](https://github.com/Aelvryx/mgba-wifi-link/issues/11) | 5 | 3 | 5 | 5 | **2.6** | Next |
| 6 | Establish CPU, memory, audio, and thermal budgets | 5 | 3 | 5 | 5 | **2.6** | Next |
| 7 | Harden Android suspend, resume, and network-change teardown | 5 | 3 | 5 | 8 | **1.6** | Next |
| 8 | [Design four-player Multi-Pak](https://github.com/Aelvryx/mgba-wifi-link/issues/10) | 8 | 3 | 8 | 13 | **1.5** | Next / research |
| 9 | Synchronize cartridge sensors | 3 | 2 | 5 | 8 | **1.3** | Later |
| 10 | Support NORMAL8/NORMAL32 and cross-ROM cable sessions | 5 | 2 | 8 | 13 | **1.2** | Later / research |
| 11 | Add Single-Pak multiboot | 8 | 3 | 8 | 21 | **0.9** | Later / research |
| 12 | Reconnection and host migration | 5 | 1 | 5 | 13 | **0.8** | Later |
| 13 | Prediction or rollback | 8 | 1 | 8 | 21 | **0.8** | Later / research |
| 14 | Wireless Adapter / RFU | 8 | 1 | 8 | 21 | **0.8** | Later / research |
| 15 | Live-session savestates | 3 | 1 | 5 | 13 | **0.7** | Later |
| 16 | GB/GBC network link | 5 | 1 | 5 | 21 | **0.5** | Outside current GBA scope |
| 17 | Internet relay / NAT traversal | 5 | 1 | 3 | 21 | **0.4** | Later |

The low scores for Single-Pak, RFU, and rollback do not mean low value. They
reflect large uncertain jobs whose first useful increment is expensive.

## Recommended execution order

### Completed — v0.2.0 usable alpha

1. **Remove v1 (#6): completed in PR #14.** The frontend selector, distributed
   runtime, obsolete tests, and v1-only analyzer are gone. Historical evidence
   remains labelled, while generic SIO and the supported versioned-runtime
   invariants have active test owners.
2. **Direct-hotspot comparison (#9): completed.** A direct 5 GHz Android
   hotspot selected one frame and remained correct, but exceeded the bilateral
   wait-free, p95, and maximum-tail publication limits. Stable remains the
   default and Low Latency remains an experimental opt-in.
3. **Publish v0.2.0 alpha (#7): completed.** The
   [exact Android ARM64 prerelease](https://github.com/Aelvryx/mgba-wifi-link/releases/tag/v0.2.0)
   includes checksums, install/upgrade notes, honest policy and compatibility
   claims, and a concise exact-artifact two-device smoke.

### Next — strengthen the product and open platforms

- Grow compatibility evidence (#8) continuously without holding future
  releases hostage to an arbitrary game count.
- Add a sanitized report bundle so users can capture build identity, frontend,
  policy, attachment, calibration, wait, and teardown evidence without leaking
  ROM, save, path, address, or input data.
- Automate release builds, checksums, provenance, and GitHub attachments from a
  reviewed tag.
- Fuzz all network- and replica-controlled decoders and retain their allocation
  bounds under sanitizers.
- Establish a repeatable performance/thermal budget before multiplying local
  replicas or adding lower-powered targets.
- Run the [H700 feasibility path](https://github.com/Aelvryx/mgba-wifi-link/issues/11).
- Harden lifecycle failure when Android backgrounds the frontend, changes
  networks, or suspends one endpoint.
- Research [four-player topology](https://github.com/Aelvryx/mgba-wifi-link/issues/10)
  in parallel, but do not commit implementation until CPU/memory/thermal and
  distributed-state costs are credible.

### Later — expand cable and cartridge scope

#### Single-Pak multiboot

Single-Pak is a distinct architecture, not a player-count toggle. The host owns
the cartridge while clients receive and execute a downloaded multiboot image
from WRAM. It therefore needs:

- asymmetric initial machine/content bundles;
- accurate BIOS/multiboot transfer and client boot state;
- a different content-identity and persistence policy;
- reset, failure, and retry semantics during download;
- redistributable multiboot fixtures before commercial qualification.

The first step is a feasibility/specification change, not production code.

#### Other link families

- NORMAL8/NORMAL32 and cross-ROM sessions need explicit identity and timing
  rules rather than relaxing exact-ROM equality globally.
- Wireless Adapter/RFU is a different peripheral and multiplayer protocol, not
  a cable mode.
- GB/GBC link is outside the current GBA product boundary and should not inherit
  assumptions from the replicated GBA pair.

#### Latency and resilience

- Prediction/rollback can reduce perceived input latency but changes save,
  audio, presentation, verification, and failure semantics.
- Reconnection, host migration, and live-session savestates all require a
  complete transferable session state—not merely keeping a socket alive.
- Internet relay/NAT traversal should follow a clear hostile-peer security model
  and should reuse frontend facilities where possible rather than adding silent
  direct sockets.

## Decision log

| Decision | Current position | Revisit when |
| --- | --- | --- |
| Production runtime | GBA Wi-Fi Link façade over the version-2 replicated session and wire contract; v1 runtime and selector removed | A new reviewed architecture demonstrates a concrete need |
| Default delay | Calibrated, minimum two frames | Exact one-frame candidate passes both-device gate |
| Network path | Ordinary LAN and direct 5 GHz Android hotspot supported; neither path guarantees lower latency | A new frontend transport path is proposed |
| Players | Exactly two | Four-player resource and topology proposal is reviewed |
| Cartridge model | Identical Multi-Pak cartridges | Single-Pak or cross-ROM proposal defines asymmetric identity |
| Serial modes | GBA MULTI for networked play | NORMAL-mode need is characterized and reviewed |
| Savestates | Rejected during a live session | Network session state has a complete serialization design |
| Internet | Trusted local network | Threat model and frontend transport path are agreed |

## How work is tracked

- The `v0.2.0 — Usable alpha` milestone contains the current release horizon.
- Issues define actionable now/next outcomes; later inventory remains here until
  it is close enough to investigate responsibly.
- OpenSpec captures architecture or behavioural changes, then is archived after
  merge.
- This file is rescored when evidence, dependencies, or product priorities
  materially change.
