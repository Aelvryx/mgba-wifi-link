# GBA Wi-Fi Link v0.2.0 — Install and Usage

## What this alpha is

GBA Wi-Fi Link is an experimental, independent mGBA fork for exactly two
players using GBA Multi-Pak play between physical devices over a local network.
It runs as a libretro core in stock RetroArch. This fork is unsupported by
upstream mGBA.

## Requirements and limits

Use the same v0.2.0 Android ARM64 core on both devices and load ROMs with
identical effective ROM bytes. This alpha supports Multi-Pak only; it does not
support Single-Pak multiboot, Wireless Adapter/RFU, internet relay,
reconnection, or host migration. Use a trusted local LAN or a direct Android
hotspot, not untrusted peers.

## Install on Android ARM64

1. Download the Android ARM64 core file and verify its SHA-256 against the
   value in `SOURCE-AND-PROVENANCE.md`.
2. In stock RetroArch, choose **Load Core → Install or Restore a Core** and
   select the downloaded core file.
3. Repeat the installation on the second device with the identical core file.
4. Load the same ROM revision on both devices.

## Host and join

1. Put both devices on the trusted local LAN or direct Android hotspot.
2. Load the game with the installed core on both devices.
3. On player one, use RetroArch's **Netplay → Host** flow.
4. On player two, use **Connect to Netplay Host** through RetroArch's normal
   host/client flow.
5. Wait for the GBA Wi-Fi Link ready message before entering the game's
   Multi-Pak menu.

Use RetroArch's ordinary network host/client flow, not its
input-synchronizing netplay modes. The core does not replace RetroArch
controller mappings.

## Latency policy

**Auto (Stable)** is the default. It calibrates the connection and uses at
least two GBA frames of input buffering. **Auto (Low Latency, Experimental)**
may allow one frame when both peers select it, but calibration can choose a
larger delay. Disconnect before changing the option because the selected delay
does not change during a live session.

## Saves and states

Normal in-game cartridge saves remain local to each player's logical
cartridge. Live-session savestates are rejected: disconnect before creating or
loading a RetroArch savestate.

## Troubleshooting

Confirm that both devices use the same core file, identical effective ROM
bytes, stock RetroArch, and a trusted local network path. Connect before
opening the game's cable menu. If the session fails or reports a mismatch,
disconnect and start a new session rather than continuing from an uncertain
link state.

## Privacy and reporting

For a public support report, include only the release or commit, core SHA-256
when using an unpublished build, RetroArch version, platform, host/client
roles, general network type (trusted LAN or direct Android hotspot, without
addresses), game title and revision identity, exact steps, and the last visible
message. Do not upload, attach, or paste endpoint, core, or RetroArch logs.
The project has not yet published a sanitized diagnostic workflow; issue #20
tracks that separate work. Do not upload commercial ROMs, BIOS files,
copyrighted extracts, private save data, addresses, or controller-input
histories. Report suspected security vulnerabilities through GitHub's private
security-reporting flow.

## Source, licence, and support

See `SOURCE-AND-PROVENANCE.md` for the release source, licence, and artifact
hashes. This independent fork is unsupported by upstream mGBA. For ordinary
support and compatibility reports, use this repository's issue forms.
Commercial ROMs are not distributed.
