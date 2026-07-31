# Wi-Fi GBA Link Netplay

This fork adds two-device GBA Multi-Pak cable emulation to the mGBA libretro
core through RetroArch's Netpacket interface. RetroArch owns LAN discovery,
connection management, and the host/join menus. The core owns the replicated
GBA machines, inputs, local link cable, verification, and failure behavior. It
does not require a RetroArch fork or direct socket configuration.

## Supported alpha scope

- Two players on the same LAN.
- GBA MULTI mode and Multi-Pak cartridges.
- Stock RetroArch with the Netpacket command-78 callback contract.
- Identical effective ROM bytes on both peers.
- Independent player inputs and save data.
- Android and desktop libretro deployments.

The alpha does not support three or four players, Single-Pak multiboot,
NORMAL8/NORMAL32 networking, RFU/Wireless Adapter, internet relay/NAT
traversal, reconnection, host migration, rollback, or savestates during a live
session.

## Why protocol v2 is real-time

Protocol v1 synchronized individual cable events across Wi-Fi. It was useful
as a correctness oracle, but a real game could perform enough serial
transactions to make both devices wait for many network round trips per frame.
That produced the severe slowdown and broken audio seen in early tests.

The release protocol is `mgba-gba-link-replicated-v2`. At attachment:

1. The host contributes the authoritative player-zero machine state.
2. The client contributes the authoritative player-one machine state.
3. Both endpoints validate and install the same P0/P1 pair.
4. Each endpoint connects its two logical GBAs to an ordinary in-process mGBA
   lockstep coordinator.
5. Only one small, reliable input packet per player per emulated frame crosses
   Wi-Fi; every cable word stays local.

The host presents P0 and the client presents P1. Each endpoint still runs both
logical machines, so serial traffic no longer changes network packet volume.
The local-role machine alone receives that device's controller, rumble,
rotation, solar sensor, video, audio, and persistent save backing. Shadow
outputs are drained without frontend callbacks and shadow saves remain in
memory.

## Build

Desktop builds use the ordinary libretro target:

```sh
cmake -S . -B build-netplay \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_LIBRETRO=ON
cmake --build build-netplay --target mgba_libretro --parallel
```

For Android arm64:

```sh
MGBA_ANDROID_NDK=/path/to/android-ndk
cmake -S . -B build-android-arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$MGBA_ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_LIBRETRO=ON
cmake --build build-android-arm64 --target mgba_libretro --parallel
```

Name the resulting Android library `mgba_libretro_android.so`. In RetroArch,
use **Load Core → Install or Restore a Core**, select the file, and then load
content with that installed core.

## Host and join

1. Put both devices on the same LAN and load the same ROM with this core.
2. On player one, open **Netplay** and choose **Host**.
3. On player two, use LAN discovery or enter the host address, then choose
   **Connect to Netplay Host**.
4. Wait for `GBA replicated link ready: player 1` on the host and
   `GBA replicated link ready: player 2` on the client.
5. Enter the game's ordinary Multi-Pak multiplayer flow.

RetroArch assigns Netpacket client ID zero to the host and one to the client.
The core maps those roles to GBA P0/primary and P1/secondary respectively.
Only one frontend controller is sampled on each physical device; RetroArch's
normal controller remapping remains authoritative.

On Android, the host IP address is the device's IPv4 address on the active
Wi-Fi network. It is normally shown in Android's details for that network.
RetroArch LAN discovery may avoid entering it manually. The core itself does
not open a separate socket or provide another address.

Connect before entering the game's cable menu, but after both devices have
loaded content. In games with leader-driven menus, player two may show `WAIT`
while player one chooses the mode, speed, course, or other settings. That is
normal. Multi-Pak means each device loads its own identical game image;
Single-Pak download play is outside this alpha's scope.

## Compatibility and session policy

The handshake compares SHA-1 and byte length over the effective ROM, not its
filename. Save contents are deliberately independent. It also validates the
versioned replica/runtime format and rejects enabled cheats or incompatible
timing policy.

The input delay is selected once from a supported two-to-eight-frame range
using the measured handshake RTT and a conservative jitter budget. It remains
fixed for the session. Each input packet carries a four-frame redundancy
window, and the core waits inside the current `retro_run()` only when the
authoritative input for that exact frame has not arrived. A successful call
therefore returns one newly rendered local-role frame and its newly generated
audio instead of repeating a stale frame.

From transport start until teardown, the core rejects savestate creation and
loading, cheat changes, and timing-sensitive variable changes. Reset tears
down the session first. Unsupported GBA serial modes continue through the
ordinary no-network behavior.

## State verification and saves

Every 60 replicated frames, both endpoints exchange canonical SHA-256 digests
for P0 and P1. The versioned digest covers CPU, memory, hardware timing, RTC
policy, save bytes, and the local lockstep-driver state. It excludes video and
audio presentation buffers, frontend pointers, file paths, logs, transport
queues, and wall-clock bookkeeping.

A mismatch fails before another input frame is authored. The log identifies
the frame, first mismatching logical player, both digests, recent input window,
protocol/runtime policy, and session ID without printing save data or private
paths.

The assigned local machine uses the endpoint's normal RetroArch save buffer.
The shadow never receives that path. Save dirty generations are tracked for
both logical machines. On teardown, the local save remains live and the
retained single core is restored to the latest quiescent state whose pair
digest both peers acknowledged. If no periodic check completed yet, the
original attachment snapshot is preserved rather than installing uncertain
state.

## Failure behavior

Packets are copied into bounded, transport-generation-scoped queues. Queue
exhaustion, oversized packets, malformed fields, sequence gaps, conflicting
input duplicates, input timeout, verification timeout, synchronous frontend
stop, and digest divergence all fail closed. Stored frontend callbacks are
never used after their generation is invalidated.

An abrupt peer exit may show a short error notification on the remaining
device. It does not leave a network cable driver or shadow core attached. The
single-player core resumes from the latest safe state described above. For a
normal exit, leave any active transfer or linking screen, then stop hosting or
close content. Do not load, create, or depend on a savestate while connected;
the core rejects those operations for every live session state.

RetroArch currently reports an explicit client disconnect to the client core
as a frontend transport stop and to the host core as peer-detached. A short
`Link failed` notification can therefore appear even for the menu's normal
**Disconnect from Netplay Host** command. The teardown is still bounded and
safe: both sides discard the pair and restore their latest jointly verified
local state. This wording describes the current frontend callback semantics,
not an invitation to continue after an unexplained mid-game link error.

## Diagnostics

Normal logs emit short attach, ten-second periodic, and teardown records. The
fields include:

- replicated frames;
- sent/received packets and bytes;
- successful state checks;
- local SIO transfers and words;
- rendezvous count, total, and maximum wall time;
- future input depth and copied-queue high-water mark;
- produced audio samples, audio frames, and empty-audio frames;
- local lockstep waits and per-core scheduler work;
- handshake RTT, jitter budget, selected delay, and attachment duration.

The continuous CC0 fixture under `tools/gba-link-test-rom` tolerates the
expected pre-attachment no-peer state, then runs all four MULTI baud selectors
indefinitely and fails closed after a real peer has been observed. This makes
late host/join runs useful for serial-throughput and audio qualification.

The legacy protocol-v1 implementation and traces remain only as a diagnostic
SIO oracle. The normal libretro registration selects protocol v2. Developers
can select **GBA Link Netplay Runtime → Cable Sync v1 (Diagnostic)** and
restart content to reproduce a v1 trace; that mode is intentionally labelled
unsuitable for gameplay.

## Current validation

All 17 focused Linux SIO, protocol-v2, replica, pair, input, save-routing, and
libretro-adapter tests pass normally, under ASan/UBSan with leak detection, and
under TSan. This includes a paired replay of two actual adapter instances with
deterministic latency/jitter and faults at every attachment, input,
verification, detach, reset, stop, and unload boundary.

A stock-RetroArch localhost soak completed 134,400 replicated frames (37
minutes 20 seconds of emulated time), 2,239 matched verification rounds, and
124,680 generic MULTI transfers. Every sampled rolling P0/P1 trace matched,
both endpoints returned audio on every completed frame with zero empty-audio
frames, packets remained frame-scaled, and serial throughput was within 0.002%
of the direct local-pair baseline. The log validator and headless RetroArch
qualification profiles are under `tools/` so the run can be repeated without
game-specific input.

Exact-head two-device Android and commercial-game qualification is recorded
separately in `docs/netplay-validation-matrix.md`; older entries in that file
are explicitly protocol-v1 historical evidence.
