## Why

mGBA already models generic local GBA link-cable transfers, but its libretro core cannot connect separate devices. Adding deterministic, Netpacket-backed link play will establish a broadly compatible foundation for multiplayer over a local Wi-Fi network without forking RetroArch or adding game-specific protocol emulation.

## What Changes

- Add an opt-in link-netplay mode to the mGBA libretro core that uses the frontend-provided Netpacket interface.
- Establish atomic two-player host/client sessions through bilateral identity exchange, exact protocol validation, a timing-sensitive determinism profile, and an explicit compatibility policy.
- Default the MVP compatibility policy to exact effective-ROM equality while reserving the protocol model for future known-compatible cross-ROM groups.
- Emulate generic two-console Multi-Pak transfers in GBA MULTI serial mode, with player zero authoritative for player assignment, committed cable-visible events, transfer timing, and results.
- Define a small versioned, reliable, ordered packet protocol with separate sequence domains for session setup, execution grants, mode barriers, transfer synchronization, results, and clean detach.
- Make execution normatively host-led: player zero reaches each cable horizon before granting player one to catch up, with at most one grant outstanding and no client grant beyond the host's current cable cycle.
- Complete each transfer through an explicit host-at-completion catch-up, client-ready, and authoritative-decision exchange; scope equal outcomes to healthy delivery of the final decision and define terminal-partition behavior.
- Commit cable-visible mode readiness through a barrier so neither peer can observe a latency-delayed mode change in its emulated past.
- Defer the blocking mode barrier for writes discovered after transfer START, allowing both peers to reach the immutable completion cycle before committing the deferred mode generation.
- Start the attachment deadline when a peer is admitted, attach only from a subsequently accepted quiescent SIO boundary, sample both current local modes as the initial mode generation, and keep the client paused after its final acknowledgement until the host releases execution.
- Separate local event scheduling from frame-oriented network execution grants and from attachment, mode, transfer-start, and transfer-completion hard barriers.
- Audit common mGBA SIO dispatch so a driver that does not handle the active mode is observationally equivalent to having no network driver for that mode.
- Define `TRANSFER_START` emission as the point after which its announced completion cycle is immutable, and complete every later success or recoverable failure at that cycle through a hardware-informed SIO transition.
- Track player one's pre-transfer wait-for-primary START condition and synchronously restore disconnected idle lines on teardown so that an idle transport failure cannot leave MULTI busy without a completion path.
- Freeze timing-sensitive configuration for every non-disconnected session state, reject both state creation and state loading throughout that lifetime, and fail closed on callback, queue, or send failures.
- Keep attached-session topology separate from the effective participant count that common SIO uses for line state and the current transfer's timing.
- Pause emulation only at required conservative synchronization boundaries, favoring correctness over latency for the first release.
- Add deterministic protocol and SIO tests, including a purpose-built GBA link test ROM and latency/jitter fault-injection coverage.
- Front-load a stock RetroArch Netpacket lifecycle and Android feasibility spike before production protocol and driver work.
- Preserve existing local lockstep behavior and keep the network implementation in a separate driver.
- Preserve upstream mGBA Git ancestry to support rebasing, review, and possible upstreaming.
- Constrain the first release to two players, Multi-Pak, GBA MULTI mode, the exact-ROM compatibility policy, same-LAN connectivity supplied by the frontend, and no reconnection or host migration.

## Capabilities

### New Capabilities

- `gba-link-netplay-session`: Establish, validate, operate, and terminate a compatible two-player GBA link session through the libretro Netpacket interface.
- `gba-multi-pak-link`: Perform deterministic two-player GBA MULTI-mode Multi-Pak transfers across the network with hardware-consistent IDs, timing, data, and interrupts.

### Modified Capabilities

None.

## Impact

- Establishes the repository history on a pinned upstream mGBA commit and records the canonical libretro header revision.
- Affects the mGBA libretro platform integration, common GBA SIO driver dispatch, network-driver lifecycle, multiplayer timing coordination, and build configuration.
- Introduces a versioned core-to-core packet protocol over the existing libretro Netpacket API; stock RetroArch remains responsible for connection and transport management.
- Requires a stable emulation-compatibility version and per-category determinism digests rather than arbitrary build or serialized-configuration hashes.
- Adds no direct socket stack and requires no RetroArch fork or bespoke Android frontend.
- Existing local link-cable operation remains available and unchanged.
- Initial compatibility excludes Single-Pak multiboot, RFU/Wireless Adapter, NORMAL8/NORMAL32 serial modes, three- or four-player sessions, internet relay/NAT traversal, reconnection, and host migration.
