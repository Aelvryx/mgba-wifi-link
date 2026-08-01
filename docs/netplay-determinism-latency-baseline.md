# Protocol-v2 Determinism and Latency Baseline

This record freezes the starting point for the
`harden-protocol-v2-determinism-latency` change before production behavior is
modified.

## Source and runtime identity

| Item | Baseline |
| --- | --- |
| Repository source commit | `57798c3f3` (`docs: sync and archive Four Swords discovery spec`) |
| Pinned upstream base | `71aa6c7dab7654bfdbbd57e696f704671a97e55d` |
| Replicated wire version | `GBA_LINK_V2_PROTOCOL_VERSION = 2` |
| Runtime compatibility | `GBA_LINK_V2_RUNTIME_COMPATIBILITY_VERSION = 1` |
| Product input-delay range | 2–8 GBA frames |
| Current Android reference artifact | commit `c9b181aa24d5f2136a6e11fca56179b5204555be`, 8,043,736 bytes, SHA-256 `14978106a3978ab4ef6ec025add82e0e9a38decda72404e3dc8c02b3373f179d` |

The historical selector consumes the host's `ACCEPT` to `ACCEPT_ACK`
interval after both replica captures have occurred:

```text
budget_us = round_trip_ms * 500 + estimated_jitter_ms * 2000
frames = ceil(budget_us / 16742) + 1
selected = clamp(frames, overlapping_minimum, overlapping_maximum)
```

The production adapter supplies a five-millisecond jitter estimate. The
`replicaCaptureInflatesHistoricalHandshakeDelay` characterization injects no
transport delay, adds twenty milliseconds to each replica capture, and proves
that the old selector nevertheless records a forty-millisecond handshake RTT
and raises the shared delay from two to three frames.

## Existing Android latency evidence

| Workload | Fixed delay | Frames / FPS | Input rendezvous host / client | Audio | Packet rate |
| --- | ---: | --- | --- | --- | --- |
| Mario Kart three-lap qualification | 5 frames | 31,200 / 60.234, 60.237 | p50 5/8 ms; p95 13/23 ms; max 84/30 ms | 0 empty frames | 61.308/61.309 packets/s |
| Continuous fixture qualification | 4 frames | 120,600 / 60.219, 60.220 | p50 21/21 ms; p95 53/46 ms; max 251/223 ms | 0 empty frames | 61.243/61.244 packets/s |

These values are preserved from `docs/netplay-validation-matrix.md`; raw
commercial content and device logs remain outside the repository.

## Deterministic-setting audit

| Effective value | Classification |
| --- | --- |
| Loaded BIOS versus HLE and effective BIOS bytes | Peer-equal BIOS/HLE category |
| `useBios`, `skipBios`, emulation compatibility, overclock and speed-hack policy | Peer-equal CPU/timing category |
| GBA idle-loop optimization | Peer-equal idle category |
| `allowOpposingDirections` | Peer-equal input-policy category |
| RTC normalization, fake-epoch arithmetic and semantics-model versions | Peer-equal RTC-policy category |
| Effective cheat-device state | Peer-equal cheat category; enabled cheats reject |
| Authoritative frame-input version and cartridge-required input mask | Peer-equal external-input category |
| Supported RTC sources, native time semantics and synchronized-input support | Negotiated local capabilities, not equality digests |
| ROM identity, savedata and RTC source/value | Existing content/player-owned state, not configuration categories |
| Frameskip | Presentation pacing only; replica execution still advances one emulated frame per authoritative runtime step |
| Video filters, colour correction, audio filtering/volume, overlays and controller mapping | Presentation or frontend input mapping; excluded |
| Compiler, ABI, build strings and paths | Excluded metadata |

Every non-disconnected protocol-v2 state must freeze the peer-equal and
negotiated values above. The implementation audit must add newly discovered
future-affecting settings to an explicit category rather than hashing an
arbitrary configuration object.

## Cartridge and peripheral audit

| Hardware / observation | Source | Protocol-v2 policy |
| --- | --- | --- |
| RTC (`HW_RTC`) | `mRTCGenericSource` / cartridge GPIO | Normalize supported per-player sources; RTC content requires bilateral signed-64-bit time semantics |
| Tilt (`HW_TILT`) | `mRotationSource.readTiltX/Y` | Reject until synchronized frame input carries tilt |
| Gyroscope (`HW_GYRO`) | `mRotationSource.readGyroZ` | Reject until synchronized frame input carries gyro |
| Solar/luminance (`HW_LIGHT_SENSOR`) | luminance peripheral or manual solar core option | Reject both physical and manual sources until synchronized |
| Rumble (`HW_RUMBLE`) | `mRumble` output | Allow; output remains local to the owned player |
| e-Reader (`HW_EREADER`) | cartridge data/input subsystem | Reject as unsupported external input in this change |
| Camera and microphone | frontend image/audio input when present | Reject if cartridge configuration requires either |
| Digital GBA keys | authoritative frame input | Supported bilaterally |

## Unchanged automated baseline

On Fedora x86-64, the focused suite passed 17/17. The complete normal suite
passed all 37 non-baseline CTest executables. `util-hash/stagedCrc32` retained
the single pinned upstream failure while its other 17 internal cases passed.
No production source had been changed when these results were recorded.

## Verification-order measurement policy

The hardened runtime measures periodic state-verification rendezvous separately
from authoritative input waits and poll-to-send time. Input authoring remains
after the verification barrier in this change. Moving it earlier could send
future input after a divergent state exists and would alter teardown evidence;
any measured opportunity is therefore follow-up work requiring its own
failure-semantics specification, not an optimization authorized here.
