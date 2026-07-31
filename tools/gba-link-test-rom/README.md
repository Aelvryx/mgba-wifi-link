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

The output is `build/gba-link-test.gba`. A reproducible copy is committed at
that path so a clean checkout can run the two-core integration test without
making an Arm toolchain a normal mGBA test dependency. Rebuilding it should
produce the same SHA-256:

```text
24b7ef2bee7ff95ebe00d487f06ff82ea10eaefa63f04760bdb71bf9c64ffbe8
```

The header tool pads the linked program to a 32 KiB power-of-two cartridge
image with erased-ROM `0xFF` bytes. This keeps the fixture on mGBA's ordinary
ROM mapping path and avoids frontend memory-map consumers having to handle a
tiny non-power-of-two homebrew image. The ARM toolchain and Python header tool
are used only to regenerate this ROM.

The ROM waits indefinitely on a green screen for the frontend-driven
attachment barrier, then shows yellow on player 1 success, blue on player 2
success, and red on a transfer failure. It contains no Nintendo-authored
program code; the standard boot-logo header data is included solely for GBA
boot compatibility.
