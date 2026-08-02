# Roadmap

This roadmap is a direction-setting document, not a promise or release date.
GitHub issues carry the concrete work; OpenSpec is used only when a change needs
an explicit behavioural contract.

## Product principles

1. Correct generic cable behaviour before game-specific workarounds.
2. Keep network latency out of individual emulated cable transactions.
3. Fail closed when inputs or deterministic policy cannot be synchronized.
4. Preserve local save ownership and transactional rollback.
5. Measure real devices, but let evidence serve the product rather than turn
   qualification into ceremony.
6. Keep human input for complex navigation and gameplay; automate everything
   mechanical and repeatable.

## Now — usable experimental release

These are the next practical steps toward a less confusing, easier-to-install
alpha.

- **[Remove cable-sync v1 from the shipped runtime](https://github.com/AnthonyStainer/mgba/issues/6).** Its diagnostic purpose is
  complete, it is not playable, and archived design/traces preserve the useful
  history. Removal should include the frontend option, registration/dispatch,
  dead production code, and obsolete tests while retaining any genuinely useful
  generic SIO coverage.
- **[Publish the current protocol-v2 foundation cleanly](https://github.com/AnthonyStainer/mgba/issues/7).** Produce a reproducible
  Android ARM64 artifact from an exact reviewed head, with install/upgrade notes,
  hashes, and an honest compatibility table.
- **[Grow compatibility evidence](https://github.com/AnthonyStainer/mgba/issues/8).** Make it easy to report successful and failed
  Multi-Pak titles without distributing copyrighted data.
- **[Measure a direct 5 GHz hotspot path](https://github.com/AnthonyStainer/mgba/issues/9).** Compare mesh LAN and direct hotspot
  calibration/input-wait metrics. Promote a one-frame floor only if the exact
  artifact passes the existing bilateral gate; otherwise retain two frames as
  the default without drama.

## Next — broader usefulness

- **[Four-player Multi-Pak](https://github.com/AnthonyStainer/mgba/issues/10).** Extend replicated topology, input ownership,
  verification, UX, and device qualification without multiplying networked
  cable transactions.
- **[Linux handheld builds](https://github.com/AnthonyStainer/mgba/issues/11).** Establish reproducible libretro builds and install
  guidance for H700-class Anbernic devices, beginning with the RG34XXSP.
- **Synchronized cartridge inputs.** Negotiate and carry sensor values where
  useful instead of rejecting tilt, gyro, or luminance titles.
- **Release automation.** Build, hash, attest, and attach supported artifacts
  from reviewed tags with minimal manual handling.

## Later — research, not commitments

- Prediction/rollback to reduce perceived input latency beyond fixed buffering.
- Single-Pak multiboot.
- GBA Wireless Adapter/RFU support.
- Reconnection and host migration.
- Internet relay or NAT traversal.

Each of these changes alters either emulation scope or distributed failure
semantics and needs evidence before an implementation promise.

## Decision log

| Decision | Current position | Revisit when |
| --- | --- | --- |
| Production runtime | Replicated protocol v2 only | v1 removal change is reviewed |
| Default delay | Calibrated, minimum two frames | Exact one-frame candidate passes both-device gate |
| Network path | Ordinary LAN supported | Direct-hotspot A/B evidence exists |
| Players | Exactly two | Four-player topology proposal is reviewed |
| Savestates | Rejected during a live session | Network session state has a complete serialization design |

## How work is tracked

- Milestones group the current release horizon.
- Issues define concrete outcomes and link back here.
- OpenSpec captures architecture or behavioural changes, then is archived after
  merge.
- This file is updated when priorities or decision gates change—not after every
  implementation detail.
