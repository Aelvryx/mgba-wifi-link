# GBA replicated-link protocol v2

Protocol v2 replaces networked SIO transactions with a deterministic pair of
local GBA replicas on each endpoint. It is intentionally incompatible with the
retired distributed-SIO protocol:

- release compatibility string: `mgba-gba-link-replicated-v2`
- wire version: exact version `2`; no automatic downgrade
- runtime compatibility version: exact version `2`; older experimental-v2
  peers are rejected during `HELLO`
- stability: experimental; compatibility may change between alpha builds
- transport: reliable, ordered RetroArch Netpacket delivery
- byte order: fixed-width little endian

Every packet has a 32-byte header containing magic, exact version, message
type, payload length, zeroed reserved word, session ID and per-sender packet
sequence. Packet sequences and snapshot generations never wrap. Typed payloads
are exposed only after role, ownership, length, reserved-byte, canonical
Boolean and message-specific relation checks succeed.

## Attachment

The attachment sequence is:

1. Both original cores stop at their first quiescent SIO boundary and remain
   paused. The attachment timeout starts at peer admission, before this wait.
2. Bilateral `HELLO` packets use header session ID zero and contain distinct,
   process-lifetime-unique connection nonces. They require exact ROM identity,
   runtime compatibility, compatible replica capabilities and a canonical
   deterministic profile. RTC and synchronized-input support are negotiated
   separately so harmless capability supersets remain compatible.
3. The host assigns a provisional session ID and calibration generation in
   `CALIBRATION_BEGIN`, bound to both connection nonces. Twelve sequential
   host probes and twelve sequential client probes are acknowledged and
   exchanged as complete reports. Replica work is outside these measurements.
4. Both peers hash the canonical 24-sample vector and run selector policy 1.
   `Auto (Stable)` supplies a two-frame product floor; `Auto (Low Latency,
   Experimental)` supplies a one-frame floor. The stricter peer policy wins.
5. `ACCEPT` promotes the provisional ID and carries the vector digest,
   min/p50/p95/max measurements, negotiated range, product floor, selection
   reason and immutable selected delay. The client recomputes all values; the
   old capture-contaminated ACCEPT/ACK duration is not a selector input.
6. Each endpoint captures only its assigned logical player: host/P0 and
   client/P1. A canonical manifest precedes bounded chunks of at most 48 KiB.
7. Each receiver enforces the manifest's resource ceilings, chunk geometry and
   SHA-256 digests before constructing a provisional P0-then-P1 pair.
8. Both endpoints acknowledge the same ordered P0/P1 bundle digests.
9. The host sends `SESSION_READY`, repeating the calibration decision; the
   client acknowledges but remains paused.
   The host commits only after the acknowledgement, and the client commits only
   after the first valid `INPUT_WINDOW` release.

The original core remains intact until final readiness. Any earlier protocol,
transport, allocation, digest, installation or deadline failure destroys only
the provisional state and invalidates the transport generation.

## Determinism profile and external inputs

Profile schema 1 is a little-endian `uint16` schema and record count followed
by at most sixteen ascending 36-byte records. Each record is a category ID,
flags (`REQUIRED` is bit zero), and a SHA-256 digest. The seven required
categories are BIOS/HLE identity, CPU/timing policy, idle optimization,
opposing-direction policy, RTC normalization versions, cheat state, and the
authoritative-input format plus cartridge-required input mask. Digests use the
domain `mgba-gba-link-replicated-v2\0determinism-profile-v1\0` and the fixed
category payloads documented in the protocol headers.

Paths, save bytes, controller mappings, presentation settings, ABI and build
metadata are excluded. Unknown required categories reject. Enabled cheats
reject before calibration. Timing-sensitive session policy is frozen until
the transport returns to `DISCONNECTED`.

Schema-1 frame input contains digital keys only. Tilt, gyroscope,
luminance/solar hardware—including manual solar control—and e-Reader card
input therefore reject before calibration with an external-input diagnostic.
Rumble is local output and does not block admission. This GBA hardware path
currently has no corresponding camera or microphone cartridge flag to detect.
Logs retain the safe profile category, capability class, and missing-input
mask, not profile digests, button history, sensor values, BIOS data, or paths.

## RTC normalization

RTC-bearing content requires signed integer `time_t` semantics of at least 64
bits on both endpoints. Non-RTC content remains portable when that capability
differs. Each logical cartridge retains its own RTC source and value; P0 and P1
need not show the same time, but both copies of P0 and both copies of P1 must.

Default wall-clock and wall-clock-offset sources are sampled once during
authoritative replica capture and converted with checked integer arithmetic to
a deterministic `RTC_FAKE_EPOCH` anchored to that player's frame. Fixed and
existing fake-epoch sources remain unchanged. Custom sources and any
arithmetic or full-`UINT32_MAX` frame-domain overflow reject before a replica
is sent.

On teardown, fixed and fake-epoch sources restore exactly. Default wall-clock
and wall-clock-offset sources restore their original source semantics and may
therefore jump relative to the rolled-back emulated checkpoint. Machine state,
save backing, cartridge RTC metadata and source fields still restore as one
transaction.

## Calibration and selector

The calibration messages have fixed payload sizes: `CALIBRATION_BEGIN` 40
bytes, `LATENCY_PROBE`/`LATENCY_ACK` 16 bytes, and `LATENCY_REPORT` 68 bytes.
Probe ordinals are 0–11 and integer-microsecond samples are 0–1,000,000.
Host and client trains are sequential and single-flight. The host calibration,
client calibration and client `WAIT_ACCEPT` each have distinct absolute,
non-refreshing three-second deadlines using a fallible monotonic clock.

Selector policy 1 sorts all 24 samples. It uses item 1 as minimum, item 12 as
p50, item 23 as nearest-rank p95 and item 24 as maximum:

```text
base      = ceil(minimum / 2)
variation = p95 - minimum
budget    = base + variation + 1000 microseconds
candidate = ceil(budget * 16,777,216 / (280,896 * 1,000,000))
selected  = max(candidate, negotiated product floor)
```

Every operation is checked integer arithmetic. A candidate above the
overlapping maximum fails rather than being clamped. RTT/2 is a buffering
heuristic, not a one-way bound; late authoritative input still enters the
bounded input rendezvous and is never predicted. Malformed, missing, stale,
out-of-range or clock-failed calibration fails closed and never falls back to
a lower delay.

RetroArch's explicit disconnect command may synchronously stop the Netpacket
transport before the core can exchange another application packet. Protocol
v2 therefore treats both a local frontend stop and the corresponding remote
peer-detached callback as bounded terminal events: each role invalidates its
callback generation, discards the replicated pair, and restores its assigned
core to the latest jointly verified state. The two roles may log different
terminal reason codes; this is not an in-session state divergence because no
later replicated frame is released.

## Runtime messages

Protocol v2 admits only fixed-delay input batches, periodic pair-state checks
and detach control after attachment. A bounded raw retired-wire regression
proves that old magic/version bytes cannot decode or dispatch inside a v2
session, either before replica capture or after readiness.

Each endpoint owns a bounded 256-frame input ring for each logical player.
Host packets may author P0 only and client packets P1 only. An initial seed
covers the delay window; thereafter one reliable flushed `INPUT_BATCH` per
endpoint and replicated frame authors `F + D` plus at most three recent
records. Complete batches are preflighted before mutation, exact duplicates
are idempotent, and a conflicting duplicate or out-of-window record terminates
the runtime. Frame `F` is consumed only when both authoritative records exist,
after which both keys are installed and the local pair advances exactly once.
Serial transfers remain entirely within the local lockstep coordinator, so
ordinary packet counts depend on frames rather than transferred words.

The copied-packet transport allocates the exact received size under a hard
64 KiB packet ceiling. Its inbound and outbound queues remain bounded to 64
packets, own all copied bytes across frontend callbacks, and fail closed on
allocation, queue, size, callback-generation or reliable-send failure.
