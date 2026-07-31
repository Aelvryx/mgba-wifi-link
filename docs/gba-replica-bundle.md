# GBA replica bundle format

Protocol v2 creates one authoritative replica bundle for each logical
cartridge. The bundle is an internal attachment format, not a user savestate.
Format version 1 is defined by `include/mgba/internal/gba/replica.h` and encoded
by `src/gba/replica.c`.

## State inventory

The canonical uncompressed payload is the concatenation of:

1. one zero-initialized `GBASerializedState` (`0x61000` bytes); and
2. the exact save-memory bytes declared by the manifest.

The existing GBA savestate contains the mutable CPU registers and pipeline,
EWRAM/IWRAM, I/O registers, palette/OAM/VRAM, DMA and timer state/events, audio
and video state, global and local timing, SIO mode/register/completion state,
save command-machine state, GPIO pin and peripheral state, and cartridge RTC
state. Zero-initializing the destination before `GBASerialize()` makes every
reserved byte canonical.

Save-memory bytes are separate because `GBASerializedState` records the save
type and active EEPROM/Flash command state but deliberately omits SRAM, Flash,
and EEPROM storage. The bundle supports no-save, SRAM, SRAM512, Flash512,
Flash1M, EEPROM, and EEPROM512 cartridges. The manifest also records the core's
generic RTC override type and value; those are frontend/core policy outside the
cartridge RTC fields in the savestate.

The bundle excludes effective ROM bytes, BIOS bytes, filenames, save paths,
frontend configuration, renderer buffers, frontend audio/video queues, host
pointers, logging data, Netpacket callbacks/queues, session state, and attached
SIO-driver state. Exact ROM, BIOS, and determinism compatibility are negotiated
before exchange; restoration additionally checks the savestate's ROM CRC and
BIOS checksum against the already-loaded target core.

## Capture boundary

Capture is accepted only for a loaded GBA core with:

- no attached SIO driver;
- MULTI busy clear; and
- no scheduled SIO completion event.

The adapter is responsible for invoking capture at its paused frontend frame
boundary. `GBAReplicaCapture()` does not run the core, alter its timing, change
its save type, or consume file position. Tests serialize the source before and
after capture and require identical state.

## Manifest

The manifest is exactly 240 bytes and uses fixed-width little-endian fields.
All unlisted bytes are reserved and must be zero.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `GBRP` |
| 4 | 2 | Bundle format version |
| 6 | 2 | Manifest byte size (`240`) |
| 8 | 4 | Emulation compatibility version |
| 12 | 4 | Flags (zero in version 1) |
| 16 | 8 | Snapshot generation |
| 24 | 1 | Logical player (`0` or `1`) |
| 25 | 1 | Encoding (`none` or zlib deflate) |
| 28 | 4 | GBA savestate version magic |
| 32 | 4 | Savestate size |
| 36 | 4 | Signed save-memory type |
| 40 | 4 | Save-memory byte size |
| 44 | 4 | Signed RTC override type |
| 48 | 8 | Signed RTC override value |
| 56 | 8 | Logical frame counter |
| 64 | 8 | Global emulated cycles |
| 72 | 4 | Uncompressed payload size |
| 76 | 4 | Encoded payload size |
| 80 | 4 | Canonical chunk size |
| 84 | 8 | Cartridge RTC last-latch time |
| 92 | 8 | Cartridge RTC offset |
| 112 | 32 | SHA-256 of savestate bytes |
| 144 | 32 | SHA-256 of save-memory bytes |
| 176 | 32 | SHA-256 of the uncompressed payload |
| 208 | 32 | SHA-256 of the encoded payload |

Version 1 caps state at `0x61000`, save memory at `0x20000`, uncompressed data
at `0x81000`, encoded data at `0x82000`, chunks at 48 KiB, and chunk count at
4096. Validation happens before allocation.

## Assembly and restoration

Chunks must begin at the manifest's canonical chunk boundaries and have the
exact expected size. An exact duplicate is idempotent. A conflicting duplicate,
partial overlap, hole, wrong player/generation, or out-of-range chunk fails the
bundle. The receiver verifies the encoded digest before decompression, requires
the compressed stream to consume and produce exactly the declared byte counts,
and then verifies the complete, state, and save digests.

Restoration uses a fresh core with the already identity-checked effective ROM.
It installs the explicit save geometry and bytes before `GBADeserialize()` so
the saved command-machine and Flash-bank state applies to the correct storage,
then restores the RTC override. The original single-player core remains the
rollback source until later session work has installed both logical replicas.
