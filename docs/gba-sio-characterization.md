# GBA MULTI SIO Characterization

Date: 2026-07-30

Status: Frozen prerequisite for the Wi-Fi link transfer implementation.

## Evidence and limits

The register tables below distinguish physical GBA guarantees, the pinned
mGBA no-driver baseline, and the deterministic policy used when a network
transfer has already become observable.

Primary references:

- Nintendo, *Game Boy Advance Programming Manual*,
  `AGB-06-0001-002-B13`, section 13.2, pp. 113-119
  ([preserved mirror](https://pdfcoffee.com/gameboy-advance-programming-manual-pdf-free.html)).
- The physical cable description and signal observations in
  [GBA Communications Information](https://www.akkit.org/info/gba_comms.html).
- [GBATEK's MULTI register description](https://mgba-emu.github.io/gbatek/#siomulti-player-mode),
  used as an independent conformance reference.
- mGBA commit `1d65391d3531f9338300f306c4e1d76c258ce657`,
  specifically `src/gba/sio.c` and `src/gba/sio/lockstep.c`.

The programming manual establishes the following behavior:

- MULTI start initializes all four receive words to `0xFFFF`.
- An absent terminal leaves its receive slot at `0xFFFF`.
- Completion clears busy/start and raises SIO IRQ when locally enabled.
- A synchronization or stop-bit fault sets the communication-error flag and
  stores invalid data, but the manual does not prescribe the exact invalid
  word for every electrical failure.
- SD high means all connected terminals are in MULTI; SI low identifies the
  primary, while an unattached pulled-up SI reads as secondary.

The akkit physical description corroborates pulled-up missing-terminal words
and SC returning high after completion. No new oscilloscope capture was made
for this fork. The executable `gba-sio` tests therefore serve as the
authoritative project conformance fixture, with the evidence level for every
case stated below. A later hardware capture may refine the post-failure policy
without changing ordinary no-driver compatibility.

## Frozen register and timing table

`preserve` means the transition does not write that field. “Enabled IRQ” means
exactly one SIO interrupt request when SIOCNT bit 14 was set locally.

| Case | `SIOMULTI0..3` after transition | Busy | Error | Ready | Slave | ID | RCNT SC | Timing/event | IRQ | Authority |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| Attached session before joint MULTI mode commit | preserve | 0 | preserve | 0 | 1 | 0 | 1 | No transfer event | None | Network attachment policy; cable topology is hidden from executing software until the committed mode generation |
| Jointly ready two-player MULTI cable | preserve | 0 | preserve | 1 | 0 for primary, 1 for secondary | 0 for primary, 1 for secondary | 1 | No transfer event | None | Programming-manual SD/SI semantics and existing mGBA local lockstep |
| Ordinary primary start with no driver/peer, immediately after start | `FFFF FFFF FFFF FFFF` | 1 | 0 | 1 | 1 | 0 | 0 | Common event at the zero-peer duration | None yet | Pinned mGBA baseline; initialization agrees with manual |
| Ordinary no-driver/no-peer completion | `0000 0000 0000 0000` | 0 | 0 | 1 | 1 | 0 | 1 | Existing common event completes | One if enabled | Pinned mGBA compatibility baseline; the zero words are not claimed as physical cable behavior |
| Secondary locally writes start while waiting for a primary | preserve | 1 | 0 | 1 | 1 | 1 | 1 | No completion event until a valid remote primary start | None | Existing mGBA secondary wait behavior and lockstep regression history |
| Idle detach, with no transfer active | preserve | 0 | preserve | 1 | 1 | 0 | 1 | No event | None | Pulled-up line model plus project detach policy |
| Recoverable failure after `TRANSFER_START(T,C)` | `FFFF FFFF FFFF FFFF` | 0 | 1 | 1 | 1 | 0 | 1 | Existing common event at immutable cycle `C` | One if enabled | Deterministic network error policy, constrained by manual error/busy/IRQ rules and pulled-up invalid-data convention |
| Reset or unload | reset state | 0 | reset state | reset state | reset state | reset state | reset state | Pending event cancelled immediately | None | Existing reset/unload semantics |

The zero-peer MULTI durations in GBA cycles are:

| Baud selector | Nominal baud | Zero-peer cycles |
| ---: | ---: | ---: |
| 0 | 9,600 | 31,976 |
| 1 | 38,400 | 8,378 |
| 2 | 57,600 | 5,750 |
| 3 | 115,200 | 3,140 |

These values freeze the pinned mGBA path only. A START emitted for the
two-device network path always uses effective peer count one and its announced
completion cycle remains immutable after emission.

Ready high alone does not distinguish a valid attached cable from disconnected
pulled-up lines: both read high. Software and tests must also observe the role
lines. A jointly ready primary has slave clear and ID zero; the supported
secondary has slave set and ID one. Detached state has slave set and ID zero.

## Required implementation boundaries

- Pre-START failure must use the ordinary no-driver/no-peer path exactly,
  including its current zero-word completion. Network code must not “improve”
  that upstream-compatible path opportunistically.
- Post-START failure is a distinct transaction. It uses the retained
  two-device completion cycle and the explicit erroneous result.
- An idle detach changes only the disconnected line/topology fields listed
  above. It does not synthesize transfer data, create an event, or raise IRQ.
- A secondary's local start bit is a wait-for-primary state. The network
  driver must not create a transfer, completion event, or IRQ from that write;
  a later valid remote START supplies the completion schedule.
- The post-START `0xFFFF` words are a stable fail-closed emulation policy, not
  a claim that the programming manual specifies those words for every possible
  mid-transfer electrical fault.

## Conformance fixture

`src/gba/test/sio.c` freezes:

- all four ordinary no-peer baud timings and pre/post-completion registers;
- the non-initiating secondary wait state;
- idle-detach and post-START error table invariants;
- common event and IRQ counts;
- same-cycle event-before-CPU-write ordering;
- latched MULTI completion dispatch after the current mode changes.

Production driver tests must reuse this table rather than restating different
abort or detach values.
