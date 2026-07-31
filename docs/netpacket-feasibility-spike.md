# Netpacket and Android Feasibility Spike

Date: 2026-07-30

Status: Phase 0 feasibility confirmed on Linux localhost and two stock Android
RetroArch devices over Wi-Fi.

## Versions and artifacts

- mGBA base:
  `1d65391d3531f9338300f306c4e1d76c258ce657`
- Canonical libretro header and RetroArch:
  `556283a6689ab5502ceec86f4e83e8b8d796bbd8`
- RetroArch: `1.22.2 (Git 556283a)`, GCC 16.1.1, x86-64
- Spike protocol: `mgba-netpacket-spike-v1`
- Spike core: x86-64 ELF shared object
- Spike core SHA-256:
  `adecb87d2000700e41631c6dc684a4c8f1574696a8c9baee6d2e5e79ef3190a6`
- Transport: stock RetroArch Netpacket over Linux localhost
- Android frontend on both devices: package `com.retroarch.aarch64`,
  `1.22.2_GIT`, version code `1763607214`; runtime log identifies
  `RetroArch 1.22.2 (Git 69a4f0e)`, built 2025-11-20
- Android devices: AYN Thor and AYN Odin2 Portal, Android 13/API 33,
  `arm64-v8a`
- Android toolchain: NDK r27 (`12077973`), Clang 18.0.1, minimum Android 21
- Android spike core: stripped AArch64 ELF shared object for Android 21
- Android spike core SHA-256:
  `bfff115e5a02a6dd76b87c3f395b23e1cb4f9c3f47411d3e7e1fd25ee464977c`
- Probe ROM SHA-256:
  `577d8afbc869892757913a2bb143b118c805c2cbd2224cc86ef2448fe4802c8c`
- Android transport: stock RetroArch Netpacket over the same Wi-Fi 6 access
  point, using a direct client-to-host LAN connection on port `55440`

The tested RetroArch build reports dynamic core loading, threads, and netplay
support enabled. It was built from the pinned, otherwise unmodified source with:

```sh
./configure \
  --prefix=/tmp/retroarch-install \
  --enable-networking \
  --disable-x11 --disable-wayland --disable-opengl --disable-vulkan \
  --disable-sdl --disable-sdl2 --disable-sdl3 \
  --disable-alsa --disable-pulse --disable-pipewire \
  --disable-udev --disable-dbus --disable-ffmpeg --disable-mpv \
  --disable-qt --disable-freetype --disable-menu --disable-cheevos \
  --disable-online_updater --disable-update_cores \
  --disable-update_core_info --disable-update_assets
make -j8
```

The spike core is opt-in and excluded from default production builds:

```sh
env PYTHONPATH=/tmp/mgba-build-tools \
  /tmp/mgba-build-tools/bin/cmake \
  -S . -B build-netpacket-spike \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_LIBRETRO=ON \
  -DENABLE_NETPACKET_SPIKE=ON -DSKIP_LIBRARY=ON \
  -DBUILD_TEST=OFF -DBUILD_SUITE=OFF \
  -DUSE_ZLIB=OFF -DUSE_MINIZIP=OFF -DUSE_PNG=OFF \
  -DUSE_LIBZIP=OFF -DUSE_SQLITE3=OFF -DUSE_LZMA=OFF \
  -DUSE_JSON_C=OFF -DUSE_FREETYPE=OFF -DUSE_FFMPEG=OFF \
  -DUSE_ELF=OFF -DUSE_LUA=OFF -DENABLE_SCRIPTING=OFF \
  -DUSE_DISCORD_RPC=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo

env PYTHONPATH=/tmp/mgba-build-tools \
  /tmp/mgba-build-tools/bin/cmake \
  --build build-netpacket-spike --parallel 8
```

The Android core was built with:

```sh
MGBA_ANDROID_NDK=/path/to/android-ndk-r27

env PYTHONPATH=/tmp/mgba-build-tools \
  /tmp/mgba-build-tools/bin/cmake \
  -S . -B build-android-spike-arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$MGBA_ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_LIBRETRO=ON \
  -DENABLE_NETPACKET_SPIKE=ON \
  -DBUILD_TEST=OFF -DBUILD_SUITE=OFF \
  -DUSE_ZLIB=OFF -DUSE_MINIZIP=OFF -DUSE_PNG=OFF \
  -DUSE_LIBZIP=OFF -DUSE_SQLITE3=OFF -DUSE_LZMA=OFF \
  -DUSE_JSON_C=OFF -DUSE_FREETYPE=OFF -DUSE_FFMPEG=OFF \
  -DUSE_ELF=OFF -DUSE_LUA=OFF -DENABLE_SCRIPTING=OFF \
  -DUSE_DISCORD_RPC=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo

env PYTHONPATH=/tmp/mgba-build-tools \
  /tmp/mgba-build-tools/bin/cmake \
  --build build-android-spike-arm64 --parallel 8
```

`tools/netpacket-spike/make-probe-rom.c` produces a quiet ARM branch-loop
fixture. It is only a frontend lifecycle fixture, not a link-cable test ROM.

## Linux localhost procedure

Host:

```sh
/tmp/retroarch-556283a6/retroarch -v \
  --config tools/netpacket-spike/retroarch-headless.cfg \
  --host --port=55440 --nick=spike-host \
  -L build-netpacket-spike/mgba_libretro.so \
  /tmp/mgba-netpacket-spike.gba
```

Client:

```sh
/tmp/retroarch-556283a6/retroarch -v \
  --config tools/netpacket-spike/retroarch-headless.cfg \
  --connect=127.0.0.1 --port=55440 --nick=spike-client \
  -L build-netpacket-spike/mgba_libretro.so \
  /tmp/mgba-netpacket-spike.gba
```

The core registers command 78 from `retro_load_game`. Both roles send
17-byte pings every 250 ms through reliable, ordered, flush-hinted packets and
acknowledge the sender directly.

## Android two-device procedure

The same stripped core and probe ROM were copied to both devices. The shared
object was named `mgba_libretro_android.so` before invoking RetroArch's
`CoreSideloadActivity`; retaining mGBA's standard core basename is necessary
for the stock frontend to associate its installed core-info record with
`.gba` content. RetroArch installed it at:

```text
/data/user/0/com.retroarch.aarch64/cores/mgba_libretro_android.so
```

The probe was launched with the stock frontend's `RetroActivityFuture`, using
the isolated `tools/netpacket-spike/retroarch-android.cfg`. The equivalent
intent extras were:

```text
LIBRETRO=/data/user/0/com.retroarch.aarch64/cores/mgba_libretro_android.so
ROM=/sdcard/Download/mgba-netpacket-spike.gba
CONFIGFILE=/sdcard/Android/data/com.retroarch.aarch64/files/mgba-netpacket-spike/retroarch-android.cfg
```

Host/join used stock RetroArch controls, not a patched frontend:

1. On the Thor, open Quick Menu, return to Main Menu, open Netplay, and choose
   **Host**.
2. On the Odin2 Portal, open Quick Menu, return to Main Menu, open Netplay,
   and choose **Connect to Netplay Host**.
3. The isolated client configuration sets the host's LAN address and port
   `55440`.

RetroArch assigned local client ID `0` to the Thor and ID `1` to the Odin,
reported the client in host connection slot zero, and completed bidirectional
reliable ping/ack delivery.

The initial spike build crashed during a second native-window initialization
when direct content launch used GL or Vulkan. Production qualification traced
that second initialization to the core calling
`RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO` from `retro_load_game`. The production
adapter now defers the audio-rate update until content loading has completed,
so the stock frontend initializes its native window once. Both Android devices
subsequently ran the production core with visible OpenGL output throughout
link play. Null video remains useful for transport stress, but is no longer a
deployment requirement.

## Observed callback and identity behavior

Host order:

```text
register accepted
start(local_id=0, role=host)
connected(client_id=1)
receive / poll / send
disconnected(client_id=1)
```

Client order:

```text
register accepted
frontend reports connection
start(local_id=1, role=client)
receive / poll / send
stop
```

Confirmed behavior:

- Local client ID `0` identifies the host.
- The first client is ID `1`.
- Host `start` occurs before a client exists.
- Host `connected` is the point at which client ID `1` becomes sendable.
- Reliable plus flush-hint ping/ack works in both directions.
- Copied inbound packets are drained from the core-owned queue.
- `poll_receive_fn` synchronously invokes `receive`; real logs contain
  `receive_queued ... polling=yes`, followed by processing at the
  `poll_receive` boundary.
- A bounded polling window observed a frontend disconnect while
  `poll_receive_fn` was active. This RetroArch revision deferred the core's
  `stop` callback until the poll function returned.
- The API-permitted synchronous `stop` case is covered separately by
  `tools/netpacket-spike/netpacket-spike-test.c`: the fake poll callback calls
  `stop` re-entrantly, the transport generation changes, and the caller neither
  polls nor uses frontend function pointers again.

Android reproduced the required lifecycle behaviors:

- Both devices accepted command 78 from `retro_load_game`, advertised
  `poll_receive_fn`, and entered generation one.
- The host received `connected(client_id=1)`; both devices delivered copied
  packets both inside and outside `poll_receive_fn`.
- Force-stopping the client while the host was polling invoked
  `disconnected(client_id=1)` 135.197 ms into the host's active 200 ms poll.
  The host remained a valid host transport, as expected.
- After reconnecting, force-stopping the host during a client poll caused
  packet receive re-entry during that poll. The Android frontend called
  `stop` 0.206 ms after the poll returned and invalidated the client transport
  generation from one to two. It did not call full transport `stop`
  synchronously on this path.
- The separate fake lifecycle test still covers the API-permitted synchronous
  `stop` re-entry case and proves no invalidated frontend function pointer is
  reused.

The standalone lifecycle test is:

```sh
gcc -std=c11 -D_GNU_SOURCE \
  -DSPIKE_POLL_INTERVAL_NS=1 -DSPIKE_POLL_WINDOW_NS=1 \
  -DMGBA_NETPACKET_SPIKE_PROTOCOL='"mgba-netpacket-spike-v1"' \
  -Isrc/platform/libretro -Wall -Wextra -Werror \
  src/platform/libretro/netpacket-spike.c \
  tools/netpacket-spike/netpacket-spike-test.c \
  -o /tmp/netpacket-spike-test
/tmp/netpacket-spike-test
```

Result: `netpacket-spike-test: pass`.

## Measurements

The unpaced null-video run is intentionally a transport stress run rather than
an emulation-performance benchmark.

Reliable localhost RTT, 32 acknowledged samples:

| Metric | Result |
| --- | ---: |
| Minimum | 0.077838 ms |
| Median | 0.084157 ms |
| Mean | 0.093335 ms |
| 95th percentile | 0.166415 ms |
| Maximum | 0.166764 ms |

Normal receive-poll probe:

| Metric | Host | Client |
| --- | ---: | ---: |
| Poll interval, minimum | 100.000066 ms | 100.000785 ms |
| Poll interval, mean | 100.041091 ms | 100.036727 ms |
| Poll interval, maximum | 100.075274 ms | 100.073203 ms |
| `poll_receive_fn` wait, maximum | 0.018940 ms | 0.007190 ms |

The explicit stop/re-entry stress configuration used a bounded 200 ms polling
window every 250 ms. Its longest observed client wait was 200.004922 ms. This
is a deliberate test bound, not localhost transport latency.

Android reliable ping/ack samples:

| Metric | Thor/host, 125 samples | Odin/client, 124 samples |
| --- | ---: | ---: |
| Minimum | 7.273 ms | 43.802 ms |
| Median | 262.707 ms | 249.686 ms |
| Mean | 224.563 ms | 220.303 ms |
| 95th percentile | 268.104 ms | 249.954 ms |
| Maximum | 280.713 ms | 249.982 ms |

These are end-to-end spike acknowledgment times, not raw Wi-Fi RTT. The spike
intentionally waits in `poll_receive_fn` for 200 ms every 250 ms and processes
the copied queue at controlled boundaries. The minimum samples demonstrate the
short path; the median is dominated by the deliberate polling policy.

Android bounded-poll measurements:

| Metric | Thor/host | Odin/client |
| --- | ---: | ---: |
| Completed polling windows | 2,692 | 127 |
| Poll wait, minimum | 200.010 ms | 200.004 ms |
| Poll wait, mean | 200.014 ms | 200.045 ms |
| Poll wait, 95th percentile | 200.023 ms | 200.065 ms |
| Poll wait, maximum | 200.064 ms | 200.080 ms |
| Poll cadence, mean | 250.072 ms | 250.077 ms |
| Poll cadence, maximum | 250.170 ms | 250.152 ms |
| Receives delivered during polling | 46 | 215 |
| Receives delivered outside polling | 204 | 36 |

No unbounded callback stall was observed. Production grants and barriers must
use measured runtime policy rather than inheriting this deliberately aggressive
spike cadence.

## Protocol-version comparison

A second core built with `mgba-netpacket-spike-v2` was connected to the v1 host.
RetroArch logged a different-core-version warning on both peers but allowed the
connection and invoked both `start` callbacks.

Therefore `protocol_version` improves frontend diagnostics but is not a
production compatibility gate in this tested frontend. The mGBA protocol must
still fail closed through its own exact wire-version and capability handshake.

## Feasibility verdict

Phase 0 is positive. The pinned Netpacket interface works from an mGBA
libretro core in stock RetroArch on Linux and on two separate AArch64 Android
handhelds over Wi-Fi. Command 78 registration, host/client IDs, connection
callbacks, reliable ordered flushed packets, bounded mid-frame polling, copied
queue processing, disconnect delivery, stop, and transport-generation
invalidation all behaved sufficiently for the production architecture.

The findings impose three implementation constraints already reflected by the
OpenSpec:

- Treat the protocol string as a diagnostic only and fail closed in the mGBA
  handshake.
- Recheck transport generation after every receive poll because callback
  re-entry is frontend- and path-dependent.
- Keep grant, barrier, and timeout cadence as measured runtime policy. The
  spike's 200/250 ms stress settings are not production defaults.

Production qualification resolved the spike's video restart issue and built
all four configured Android libretro ABIs (`arm64-v8a`, `armeabi-v7a`, `x86`,
and `x86_64`). The physical device pair remains AArch64, as expected.

## Quarantine

The spike is isolated behind `ENABLE_NETPACKET_SPIKE=OFF` by default.
`netpacket-spike.c` is removed from the libretro source list when the option is
off, and normal cores contain no `mNetpacketSpike` symbols. The fixture,
headless config, lifecycle test, and this report preserve the reusable findings
without putting temporary packet behavior in production builds.
