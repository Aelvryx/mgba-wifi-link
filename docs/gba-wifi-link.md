# GBA Wi-Fi Link

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

The runtime and wire format are explicitly experimental. RTC-bearing games use
per-cartridge deterministic session epochs on supported 64-bit-time platforms.
Cartridges requiring tilt, gyro, luminance/solar, or e-Reader card input are
rejected before connection because those values are not yet carried in
authoritative frame input. Rumble remains local output and is allowed. The
current cartridge-hardware metadata has no equivalent camera or microphone
flag in this admission path.

Current game evidence is deliberately narrower than the generic cable
architecture:

| Workload | Status |
| --- | --- |
| Continuous diagnostic ROM | Verified |
| LinkCable compatibility workload | Verified |
| Mario Kart: Super Circuit Multi-Pak | Verified — full race |
| Advance Wars | User playtest passed |
| Zelda: Four Swords | Verified — discovery and extended shared gameplay |
| Other Multi-Pak titles | Untested |

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
The local-role machine alone receives that device's controller, rumble, video,
audio, and persistent save backing. Shadow outputs are drained without
frontend callbacks and shadow saves remain in memory. Rotation, solar, and
e-Reader cartridges fail admission rather than feeding endpoint-local input
to only one replica.

## Build

Desktop builds use the ordinary libretro target:

```sh
cmake -S . -B build-gba-wifi-link \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_LIBRETRO=ON
cmake --build build-gba-wifi-link --target mgba_libretro --parallel
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
4. Wait for `GBA Wi-Fi Link ready: player 1` on the host and
   `GBA Wi-Fi Link ready: player 2` on the client.
5. Enter the game's ordinary Multi-Pak multiplayer flow.

The core option **GBA Wi-Fi Link Latency** controls the immutable product
floor negotiated at connection:

- **Auto (Stable)** is the default and requires at least two GBA frames of
  input buffering (about 33.5 ms before display/audio pipeline latency).
- **Auto (Low Latency, Experimental)** permits a one-frame floor (about 16.7
  ms) only when both peers choose it. A one-frame result remains unqualified
  until an exact Android artifact passes the documented 30-minute wait-tail
  and commercial-game gates.

The measured network target can still select a larger delay. If either peer
uses Stable, the negotiated floor is two frames. Changing the option while a
session is live cannot alter that session; disconnect first.

GBA Wi-Fi Link uses the versioned `mgba-gba-link-replicated-v2` Netpacket
contract. Older configurations may still contain an
`mgba_gba_link_netplay_runtime` line with any value; the core no longer
declares or queries that key, so the line is inert and may be removed.

The recorded Thor/Odin mesh-Wi-Fi run selected two frames under both policies.
That is the current qualified outcome: Low Latency was active, but its clean
calibration correctly declined to commit one frame on that network path.

RetroArch assigns Netpacket client ID zero to the host and one to the client.
The core maps those roles to GBA P0/primary and P1/secondary respectively.
Only one frontend controller is sampled on each physical device; RetroArch's
normal controller remapping remains authoritative.

On Android, the host IP address is the device's IPv4 address on the active
Wi-Fi network. It is normally shown in Android's details for that network.
RetroArch LAN discovery may avoid entering it manually. The core itself does
not open a separate socket or provide another address.

An Android local hotspot is also an ordinary supported IP LAN: one handheld
hosts the hotspot and Netplay server, the other joins it, and the client uses
the hotspot owner's address. This can reduce forwarding or mesh-backhaul
jitter, but calibration remains authoritative. Wi-Fi Direct is present on the
qualified devices, but stock RetroArch does not create or manage a Wi-Fi Direct
group; Android would first need to expose such a link as mutually reachable IP
interfaces. Prefer a 5 GHz hotspot for a future controlled A/B test.

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

The input delay is selected once from a supported one-to-eight-frame range.
Before replica capture, twelve clean RTT probes initiated by each role produce
a canonical 24-sample vector. The selector uses minimum transit, p95 variation,
a one-millisecond scheduling guard, the exact GBA frame period, and the
negotiated product floor. Replica capture and installation are deliberately
outside that measurement. It remains fixed for the session. Each input packet
carries a four-frame redundancy window, and the core waits inside the current
`retro_run()` only when the authoritative input for that exact frame has not
arrived. A successful call therefore returns one newly rendered local-role
frame and its newly generated audio instead of repeating a stale frame.

The handshake also compares explicit BIOS/HLE, CPU/timing, idle optimization,
opposing-direction, RTC-normalization, cheat, and external-input categories.
An actionable mismatch appears before either original core is mutated. Default
wall-clock RTC is normalized separately for P0 and P1 during the session and
restores its original wall-clock semantics on teardown.

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
both logical machines. Every accepted local checkpoint atomically captures
the serialized machine and save-controller state, the complete frontend save
backing, RTC metadata, frame, and save generation. On teardown, all of that
checkpoint is restored together. If no periodic check completed yet, the
attachment checkpoint and its save bytes are restored rather than retaining
uncertain session writes.

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
- calibration identity and digest, 24-sample min/p50/p95/max, selector policy,
  product floor, selected delay, and attachment duration;
- per-endpoint wait-free ratio, input-wait p95 and maximum tail, input deadline
  misses, insertion lead, poll-to-send timing, and separate verification waits.

These are bounded aggregates. Logs do not contain button history, ROM bytes,
save bytes, network addresses, or private filesystem paths. Commercial ROMs,
saves, controller scripts, screenshots, and raw device evidence remain in the
ignored private qualification directory; only approved identity digests and
aggregate measurements belong in repository documentation.

The continuous CC0 fixture under `tools/gba-link-test-rom` tolerates the
expected pre-attachment no-peer state, then runs all four MULTI baud selectors
indefinitely and fails closed after a real peer has been observed. This makes
late host/join runs useful for serial-throughput and audio qualification.

The retired protocol-v1 implementation is not compiled, registered, or
selectable. Its labelled historical traces and performance findings remain in
the repository and Git history as architectural evidence; current diagnostics
exercise protocol v2 and the local replicated pair directly.

## Physical-device test ownership

Every physical-device run must be assigned to one of two operating modes
before it starts:

- **Automation-owned:** reproducible build, hashing, installation, scripted
  launch and connection, purpose-built fixtures, unattended soaks, fault
  injection, log capture and analysis, teardown, and disposable-file cleanup.
- **Human-owned:** game menus, sustained controller input, gameplay, visual or
  audio judgement, save selection, and any other interaction whose correctness
  depends on understanding a changing screen.

Do not use an iterative screenshot/tap/key loop to imitate human gameplay or
explore an unfamiliar frontend menu. Automation may execute a short, already
verified setup sequence. If the expected screen or control state differs, it
must stop and hand the prepared devices to the human tester with a short action
list. It must not change menu drivers, hotkeys, controller mappings, or normal
user configuration as an improvised workaround.

Before a human-owned run, automation prepares the exact build, isolated test
paths, synchronized logging, run identifier, time box, expected success state,
and failure signals. During the run it monitors and captures diagnostics
without driving the game. After the tester reports success or failure,
automation owns evidence extraction, analysis, teardown, and cleanup. This
keeps scarce human interaction focused and prevents slow hybrid testing that
is neither repeatable automation nor effective manual play.

### Canonical Android controller preflight

Never use `adb shell input`, injected key events, or injected taps before or
during a controller-qualified RetroArch process. Android exposes those events
through a synthetic `Virtual` input device. RetroArch can assign that device to
port 1 and move the handheld's physical AYN controller to port 2, leaving the
game apparently unresponsive despite an otherwise valid configuration.

For every human-assisted Android run:

1. Clone each device's current normal RetroArch config; do not substitute a
   generic controller or menu profile.
2. Change only isolated save/state/log paths and documented diagnostic options.
3. Enable the existing touchscreen overlay on both endpoints as a fallback,
   without changing its layout or physical bindings.
4. Launch content directly without any ADB input injection.
5. Have the human press one physical button on each device.
6. Require the log to show the real AYN controller `configured in port 1` on
   each endpoint and capture one private screenshot proving both overlays are
   visible.
7. Only then perform host/join and game navigation. On this stock Android
   frontend, host/join remains human-owned; do not replace it with improvised
   hotkeys or menu-driving automation.

If a log instead shows `Virtual ... configured` or the AYN device in port 2,
stop RetroArch and relaunch cleanly. Do not compensate by changing the joypad
index: the injected virtual device is transient and the workaround will break
the next clean launch. The Four Swords change provides the checked helper at
`tools/four-swords-discovery/android-qualification.sh`.

## Current validation

All 13 focused Linux SIO, protocol-v2, replica, pair, input, save-routing, and
GBA Wi-Fi Link façade tests pass normally, under ASan/UBSan with leak
detection, and under TSan. This includes a paired replay of two actual adapter
instances with deterministic latency/jitter and faults at every attachment, input,
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
separately in `docs/gba-wifi-link-validation-matrix.md`; older entries in that file
are explicitly protocol-v1 historical evidence.

The post-review release candidate at `c9b181aa2` also passed a fresh physical
Android continuous-link smoke and a 15,600-frame Mario Kart Multi-Pak gameplay
smoke. Both ran at approximately 60.24 FPS with matching sampled state traces,
normal audio delivery, and no serial, timeout, or divergence fault. The same
alpha.2 runtime subsequently passed Four Swords discovery and brief shared
gameplay over 27,000 synchronized frames at approximately 60.28 FPS, with
110,852 completed transfers, matching sampled traces, normal audio, and no
protocol, SIO, timeout, or divergence fault.
