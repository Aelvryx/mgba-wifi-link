# GBA replicated-link protocol v2

Protocol v2 replaces networked SIO transactions with a deterministic pair of
local GBA replicas on each endpoint. It is intentionally incompatible with the
retained distributed-SIO diagnostic protocol:

- release compatibility string: `mgba-gba-link-replicated-v2`
- diagnostic compatibility string: `mgba-gba-link-netplay-v1`
- wire version: exact version `2`; no automatic downgrade
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
2. Bilateral `HELLO` packets require exact ROM identity, runtime compatibility,
   replicated-pair capabilities, compatible encodings and overlapping input
   delay ranges.
3. The host assigns session and snapshot generations in `ACCEPT`; the client
   acknowledges them before mutable state is sent.
4. Each endpoint captures only its assigned logical player: host/P0 and
   client/P1. A canonical manifest precedes bounded chunks of at most 48 KiB.
5. Each receiver enforces the manifest's resource ceilings, chunk geometry and
   SHA-256 digests before constructing a provisional P0-then-P1 pair.
6. Both endpoints acknowledge the same ordered P0/P1 bundle digests.
7. The host sends `SESSION_READY`; the client acknowledges but remains paused.
   The host commits only after the acknowledgement, and the client commits only
   after the first valid `INPUT_WINDOW` release.

The original core remains intact until final readiness. Any earlier protocol,
transport, allocation, digest, installation or deadline failure destroys only
the provisional state and invalidates the transport generation.

## Runtime messages

Protocol v2 admits only fixed-delay input batches, periodic pair-state checks
and detach control after attachment. Protocol-v1 grant, mode, transfer and
completion messages have different magic/version semantics and cannot decode
inside a v2 session.

The copied-packet transport allocates the exact received size under a hard
64 KiB packet ceiling. Its inbound and outbound queues remain bounded to 64
packets, own all copied bytes across frontend callbacks, and fail closed on
allocation, queue, size, callback-generation or reliable-send failure.
