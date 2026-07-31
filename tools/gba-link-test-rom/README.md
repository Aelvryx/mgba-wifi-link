# GBA Multi-Pak link test ROM

This CC0 test program exercises two-player GBA MULTI mode at all four baud
rates. The primary leaves a short emulated-cycle preparation window before
each START so the secondary can stage its outgoing word, mirroring the
software-ready phase used by real link protocols. Each peer reports a fixed
result block at `0x02000000`, including
observable attachment, player ID, effective participant count, transferred
words, busy observations, SIO register state, completion-loop counts, and
missed or duplicate serial IRQ flags.

Build it with a freestanding GNU Arm Embedded toolchain:

```sh
make CROSS=arm-none-eabi-
```

The normal generated output is `build/gba-link-test.gba`. Defining
`GBA_LINK_CONTINUOUS` produces `build/gba-link-continuous.gba`, which repeats
the checked transfer matrix indefinitely for scheduler and performance tests.
The continuous variant tolerates the ordinary no-peer state before frontend
attachment, then fails closed after it has observed a real peer. This permits
late host/join qualification without hiding failures in an established link.
Its payload words identify the cable role but deliberately do not depend on a
cartridge-local loop counter: two physical frontends may reach the attachment
barrier at different instruction phases, and that phase skew is not a cable
data error. Transfers are paced at a shared video-frame boundary so the soak
measures the same sustained serial workload from reset and from a late
frontend attachment. The finite one-shot fixture retains changing
baud/transfer payloads for strict word-order correctness coverage.
Reviewed fixtures are committed separately under `fixtures/` so a clean
checkout can run the two-core tests without making an Arm toolchain a normal
mGBA test dependency. The generated `build/` directory remains ignored.
Rebuilding the normal fixture should produce the same SHA-256:

```text
2f662e4bcf2ac81c438ae5eacc786b2d2984c00807d3656b4990da66a99edc13
```

The continuous fixture should produce:

```text
c1fe01752d4f5863d6e3e1a9866b061aaadf2927ccc3df31ba9ecbf4bc68fe9d
```

CI rebuilds the ROM with the GNU Arm Embedded toolchain and compares every
byte with the reviewed fixture. Run the same check locally with:

```sh
make verify-fixture CROSS=arm-none-eabi-
```

After an intentional source change, inspect the generated image and replace
the fixture explicitly with `make update-fixture`.

The header tool pads the linked program to a 32 KiB power-of-two cartridge
image with erased-ROM `0xFF` bytes. This keeps the fixture on mGBA's ordinary
ROM mapping path and avoids frontend memory-map consumers having to handle a
tiny non-power-of-two homebrew image. The ARM toolchain and Python header tool
are used only to regenerate this ROM.

The finite ROM waits on a green screen for the frontend-driven attachment
barrier, then shows yellow on player 1 success, blue on player 2 success, and
red on failure. The unattended continuous variant deliberately keeps a black
screen while waiting and running, and uses only a low-luminance red failure
screen, to avoid leaving a bright static image on OLED qualification devices.
It contains no Nintendo-authored program code; the standard boot-logo header
data is included solely for GBA boot compatibility.
