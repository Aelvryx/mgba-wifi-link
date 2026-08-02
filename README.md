# mGBA Wi-Fi Link

[![GBA Wi-Fi link netplay](https://github.com/Aelvryx/mgba/actions/workflows/netplay-ci.yml/badge.svg)](https://github.com/Aelvryx/mgba/actions/workflows/netplay-ci.yml)
[![Latest prerelease](https://img.shields.io/github/v/release/Aelvryx/mgba?include_prereleases&label=prerelease)](https://github.com/Aelvryx/mgba/releases)
[![License: MPL-2.0](https://img.shields.io/badge/license-MPL--2.0-blue.svg)](LICENSE)

An experimental [mGBA](https://github.com/mgba-emu/mgba) fork for two-player
GBA Multi-Pak play between physical devices over a local network. It runs as a
libretro core in stock RetroArch and uses RetroArch's Netpacket transport—no
RetroArch fork and no direct socket configuration required.

This repository is an independent experimental fork. It is not an official
mGBA release and is not supported by the upstream mGBA project.

## What works

- Two physical devices, one host and one client.
- Generic GBA `MULTI`-mode Multi-Pak cable emulation.
- Replicated local P0/P1 execution, avoiding a Wi-Fi round trip for each cable
  word.
- Deterministic configuration negotiation, RTC normalization, periodic state
  verification, and bounded failure recovery.
- Clean transport calibration with a qualified two-frame default.
- Android ARM64 through stock RetroArch; Linux libretro builds are supported by
  the source and CI.

Qualified workloads include the continuous diagnostic fixture, the LinkCable
compatibility workload, a complete Mario Kart: Super Circuit VS race, extended
Zelda: Four Swords play, and an Advance Wars user playtest. Compatibility is
still experimental and untested games remain untested.

## Current limits

- Exactly two players.
- Multi-Pak only: no Single-Pak multiboot.
- No Wireless Adapter/RFU support.
- No live-session savestates, reconnection, host migration, or internet relay.
- Tilt, gyro, solar/luminance, and e-Reader input are rejected because they are
  not synchronized yet. Rumble remains local output.
- Both devices need the same effective ROM bytes and compatible emulator
  settings. Commercial ROMs are never distributed here.

## Install the Android core

1. Download the newest Android ARM64 core from [Releases](https://github.com/Aelvryx/mgba/releases).
2. Put the `.so` file in Android's Downloads folder.
3. In RetroArch, choose **Load Core → Install or Restore a Core** and select it.
4. Repeat on the second device using the identical core file.
5. Load the same ROM revision on both devices.

The latest published binary may lag behind `master`. Release notes identify the
exact source commit and SHA-256 for every supported artifact.

## Play over Wi-Fi

1. Put both devices on the same LAN or 5 GHz Android hotspot.
2. Load the game with this core on both devices.
3. On player one, use RetroArch's **Netplay → Host** flow.
4. On player two, choose **Connect to Netplay Host** and select or enter the
   host address.
5. Wait for the replicated-link-ready message before entering the game's
   Multi-Pak menu.
6. Let player one make any leader-only selections required by the game.

Use RetroArch's ordinary network-host/client flow, not its input-synchronizing
netplay modes. Controller setup belongs to RetroArch; this core does not replace
frontend controller mappings.

### Latency policy

**Auto (Stable)** is the qualified default. It calibrates the connection and
uses a minimum two-frame input buffer. **Auto (Low Latency, Experimental)** may
permit one frame, but the measured path may still select two or more.

The current Thor/Odin mesh-Wi-Fi qualification selected two frames under both
policies. One-frame operation will not become the default until the same build
passes the documented long-run gate on both endpoints. A direct 5 GHz hotspot
comparison is tracked in the roadmap.

## Saves and states

Normal in-game cartridge saving belongs to each player's logical cartridge and
is supported. The remote shadow save is never persisted locally. On uncertain
teardown, machine state and save bytes roll back together to the last accepted
checkpoint.

Creating or loading a RetroArch savestate while a link session is live is
rejected. Disconnect first.

## Compatibility

| Workload | Result |
| --- | --- |
| Continuous diagnostic ROM | Verified |
| LinkCable compatibility workload | Verified |
| Mario Kart: Super Circuit | Verified—complete three-lap VS race |
| Zelda: Four Swords | Verified—discovery and extended shared gameplay |
| Advance Wars | User playtest passed |
| Other Multi-Pak games | Untested |

Please report both success and failure. Good compatibility reports are useful
even when no bug is present.

## Build from source

For a local Linux libretro build:

```sh
cmake -S . -B build-netplay \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_LIBRETRO=ON
cmake --build build-netplay --target mgba_libretro --parallel
```

Android release builds use the pinned toolchain and commands documented in
[UPSTREAM.md](UPSTREAM.md). Pull requests run focused normal, ASan/UBSan and
TSan suites, the complete applicable mGBA suite, fixture reproduction, helper
tests, and an Android ARM64 build.

## Project navigation

- [Roadmap](ROADMAP.md)
- [Installation, operation, and troubleshooting](docs/wifi-link-netplay.md)
- [Validation matrix](docs/netplay-validation-matrix.md)
- [Protocol-v2 design](docs/gba-link-protocol-v2.md)
- [Pinned upstream and libretro revisions](UPSTREAM.md)
- [Contributing](CONTRIBUTING.md)
- [Support and issue routing](SUPPORT.md)
- [Security policy](SECURITY.md)

The main implementation lives in `src/gba/sio/netplay`; the libretro adapter is
`src/platform/libretro/netpacket-v2.c`. Tests are under `src/gba/test` and
`src/platform/test`. Completed design changes are retained under
`openspec/changes/archive`.

## Roadmap at a glance

The immediate engineering priorities are:

1. Validate and package the v2-only deterministic/calibrated runtime as a
   clean release.
2. Test direct-hotspot latency and decide whether one-frame buffering can
   become the default.
3. Expand compatibility reporting and diagnostic capture.

Four-player Multi-Pak and Linux handheld support follow. Rollback/prediction,
Single-Pak, and RFU are later research, not promises. See [ROADMAP.md](ROADMAP.md)
for the decision gates and linked work.

## Upstream and license

This work preserves upstream mGBA ancestry and regularly rebases or merges from
the pinned upstream history described in [UPSTREAM.md](UPSTREAM.md). Generic
emulator fixes should be suitable for upstream where practical; fork-specific
product and Netpacket policy remain clearly separated.

mGBA and this fork are licensed under the [Mozilla Public License 2.0](LICENSE).
The mGBA name belongs to the upstream project; “mGBA Wi-Fi Link” is a descriptive
name for this experimental fork, not an upstream endorsement.
