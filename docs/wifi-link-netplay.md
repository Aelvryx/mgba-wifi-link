# Wi-Fi GBA Link Netplay

This fork adds two-device GBA Multi-Pak cable emulation to the mGBA libretro
core using RetroArch's Netpacket interface. RetroArch owns the LAN connection
and menus; the core owns GBA SIO timing, synchronization, transferred words,
and failure behavior. No RetroArch fork or direct socket configuration is
required.

## Supported MVP

- Two players on the same LAN.
- GBA MULTI mode and Multi-Pak only.
- Stock RetroArch with Netpacket command 78 and its current callback contract.
- Reliable, ordered, flushed core packets.
- Identical effective ROM bytes on both peers.
- Independent save memory and inputs.
- Android and desktop libretro deployments.

The MVP does not support three or four players, Single-Pak multiboot,
NORMAL8/NORMAL32 networking, RFU/Wireless Adapter, internet relay/NAT
traversal, reconnection, host migration, rollback, or savestates during any
live network session.

## Build

Desktop builds use the ordinary mGBA libretro target:

```sh
cmake -S . -B build-netplay \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_LIBRETRO=ON
cmake --build build-netplay --target mgba_libretro --parallel
```

An Android ABI can be selected with the NDK toolchain:

```sh
MGBA_ANDROID_NDK=/path/to/android-ndk-r27
cmake -S . -B build-android-arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$MGBA_ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_LIBRETRO=ON
cmake --build build-android-arm64 --target mgba_libretro --parallel
```

Production qualification builds passed for `arm64-v8a`, `armeabi-v7a`, `x86`,
and `x86_64`. The physical qualification devices use `arm64-v8a`.

## Install on Android

Name the built library `mgba_libretro_android.so`. Install it through
RetroArch's **Load Core → Install or Restore a Core** flow, or use the stock
Android sideload activity:

```sh
adb push mgba_libretro_android.so /sdcard/Download/
adb shell am start \
  -n com.retroarch.aarch64/com.retroarch.browser.debug.CoreSideloadActivity \
  --es LIBRETRO /sdcard/Download/mgba_libretro_android.so \
  --es ROM /sdcard/Download/game.gba
```

The tested AArch64 package installs the core at:

```text
/data/user/0/com.retroarch.aarch64/cores/mgba_libretro_android.so
```

Each device needs its own legal copy of the content and may keep independent
save data. The loaded bytes, not filenames, are compared.

## Host and join

1. Put both devices on the same LAN and load the same ROM with this core.
2. On player one, open RetroArch's Main Menu, choose **Netplay**, then
   **Host**.
3. On player two, set the host address/port if LAN discovery is not used.
4. On player two, choose **Netplay → Connect to Netplay Host**.
5. Wait for `GBA link ready: player 1 (host)` and
   `GBA link ready: player 2 (client)`.

RetroArch assigns frontend client ID zero to the host and ID one to the client.
The core exposes GBA device ID zero/primary on the host and ID one/secondary on
the client only after the acknowledged attachment and initial mode generation
commit.

## Compatibility and frozen state

The protocol name is `mgba-gba-link-netplay-v1`. Its own handshake—not
RetroArch's informational core-version comparison—is authoritative.

The MVP accepts only `EXACT_ROM`: SHA-1 and byte length of the effective loaded
ROM must match. The protocol reserves a compatibility-group policy for future
cross-title or cross-revision combinations, but version one never accepts it.

The handshake also compares stable per-category determinism digests for:

- BIOS/HLE selection and BIOS identity;
- CPU timing, idle optimization, overclock, and speed-hack behavior;
- RTC override mode;
- disabled cheat state;
- the emulation compatibility version.

Compiler, Android ABI, harmless build metadata, save contents, inputs, visual
options, audio filters, and the runtime RTC value are excluded.

From transport start until complete teardown, timing-sensitive core-variable
changes and cheat API requests are rejected. Both `retro_serialize` and
`retro_unserialize` fail in every non-disconnected session state. Reset first
tears down the link; serialization while disconnected is unchanged.

## Timing and completion behavior

Player zero leads execution. It runs to a candidate cable horizon before
granting player one permission to catch up, with at most one outstanding
grant. The defaults are:

| Policy | Default |
| --- | ---: |
| Local scheduler quantum | 4,096 GBA cycles |
| Candidate horizon | 280,896 cycles, one GBA frame |
| Periodic health barrier | Disabled |

A transfer uses `START → READY → COMMIT`, followed by the host-led
`COMPLETION_CATCHUP → COMPLETION_READY → COMPLETION_DECISION →
COMPLETION_DECISION_ACK` release. `TRANSFER_COMMIT` contains candidate words
but is not a grant and is not the outcome commit point. Common mGBA completion
still installs receive registers, clears busy, and raises each enabled local
SIO IRQ.

A network pause raised inside an mGBA timing callback interrupts that timing
pass immediately. This prevents a queued completion event from running before
the corresponding network catch-up authorization.

The ready bit is high both for a jointly ready cable and for disconnected
pulled-up lines. Role lines distinguish them:

| State | Ready | Slave | ID |
| --- | ---: | ---: | ---: |
| Jointly ready primary | 1 | 0 | 0 |
| Jointly ready secondary | 1 | 1 | 1 |
| Attached, mode not jointly committed | 0 | 1 | 0 |
| Detached/pulled up | 1 | 1 | 0 |

Topology and transfer participation remain separate. An acknowledged session
has one topological peer, while the effective transfer peer count is zero
until both peers have committed MULTI readiness and one for every emitted
START.

## Failure behavior

Failure before `TRANSFER_START` emission follows mGBA's ordinary zero-peer
path. Once START is emitted, its announced completion cycle is immutable.
Recoverable failure completes there with:

- all four receive words set to `0xFFFF`;
- busy clear and communication error set;
- ready one, slave one, and ID zero;
- RCNT SC high;
- exactly one local SIO IRQ when enabled;
- detached topology for subsequent writes.

Reset and unload cancel immediately without fabricating a completion IRQ.

`COMPLETION_DECISION` is the outcome commit point. If the transport dies after
the host commits it but before the client receives it, a successful host and
erroneous client are an explicitly permitted terminal observation. If the
client received the decision but only its final acknowledgement is lost, both
retain that same outcome and the host closes the session so another transfer
cannot begin.

## Deadlines and diagnostics

| Operation | Default |
| --- | ---: |
| Handshake | 1,500 ms |
| Attachment | 3,000 ms |
| Grant | 1,500 ms |
| Mode barrier | 1,500 ms |
| Transfer readiness | 3,000 ms |
| Transfer commit | 3,000 ms |
| Completion catch-up/readiness/decision | 3,000 ms each |
| Graceful detach | 1,000 ms |

Frontend messages stay short; verbose mGBA logs identify the failed operation,
role, session/transfer state, sequences, cycles, and transport generation.
Queue exhaustion, oversized packets, send failure, invalid ordering, stale
generation, and malformed protocol input all fail closed rather than becoming
silent packet loss.

## Qualification

The CC0 fixture in `tools/gba-link-test-rom` completes 16 transactions on each
peer across every MULTI baud selector and checks words, busy/error/ready/slave
state, IDs, and missed/duplicate IRQs.

The final stock-RetroArch Wi-Fi run used an AYN Thor and AYN Odin2 Portal and
also ran afska's independently authored MIT-licensed LinkCable `basic` example
from release `v8.0.3`. Both devices reported `Players: 2`; a captured
continuous run completed more than 150 rapid back-to-back transfers. Full
evidence and commands are in `docs/netplay-validation-matrix.md`,
`docs/netpacket-feasibility-spike.md`, and
`docs/gba-sio-characterization.md`.
