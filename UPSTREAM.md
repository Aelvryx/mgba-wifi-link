# Upstream and Patch-Stack Record

## Branch roles and mGBA bases

- Upstream repository: `https://github.com/mgba-emu/mgba.git`
- Upstream source branch: `master`
- Upstream-facing branch: `feature/wifi-link-netplay-upstream`
- Upstream-facing base: `71aa6c7dab7654bfdbbd57e696f704671a97e55d`
- Upstream-facing base date: `2026-07-30T23:01:50-07:00`
- Upstream-facing base subject: `Qt: Remove unneeded Qt Multimedia-only include`
- Integration/evidence branch: `feature/wifi-link-netplay`
- Integration branch base: `1d65391d3531f9338300f306c4e1d76c258ce657`
- Initial OpenSpec/project-control commit: `7602ae97b`

The repository was initialized directly from mGBA Git history. The published
integration branch retains the complete OpenSpec, feasibility, device-evidence,
and release history so the reviewed PR and alpha tag are not force-rewritten.
The upstream-facing branch descends from the selected current upstream commit
and contains eight focused product, test, CI, and documentation commits:

```sh
git fetch upstream master --tags
git switch -c feature/wifi-link-netplay-upstream \
  71aa6c7dab7654bfdbbd57e696f704671a97e55d
```

Project-only `.codex` helpers, OpenSpec artifacts, and the temporary feasibility
spike are deliberately absent from that upstream-facing stack. No mGBA source
snapshot was imported. Future upstream refreshes must rebase the clean stack
onto an explicitly selected upstream commit, update this record, and rerun the
recorded baseline and feature tests.

## Patch-stack conventions

- Keep upstream-generic SIO dispatch and hardware-characterisation changes in
  focused commits that can be reviewed independently.
- Keep the transport-neutral codec/session logic separate from the libretro
  Netpacket adapter and from `GBASIONetplayDriver`.
- Keep temporary feasibility-spike code out of production build paths after its
  findings and reusable lifecycle tests have been preserved.
- Do not mix generated artifacts, build directories, local ROMs, device logs, or
  captures into source commits.
- Rebase rather than merge upstream into this feature branch unless preserving a
  published integration point requires otherwise.
- Record each new upstream base and canonical libretro header revision in this
  file before changing code that depends on them.

## Canonical libretro header

- Canonical repository: `https://github.com/libretro/RetroArch.git`
- Canonical source branch: `master`
- Pinned revision: `556283a6689ab5502ceec86f4e83e8b8d796bbd8`
- Canonical path: `libretro-common/include/libretro.h`
- Canonical URL:
  `https://raw.githubusercontent.com/libretro/RetroArch/556283a6689ab5502ceec86f4e83e8b8d796bbd8/libretro-common/include/libretro.h`
- Vendored destination: `src/platform/libretro/libretro.h`
- Canonical download SHA-256:
  `206fb1197b03adb179410ccc158793d6cec2209075a92a02e0c74b62565647b1`

The pinned canonical header must define environment command 78 and the
Netpacket callback signatures used by the adapter. Header provenance and its
content digest are deliberately recorded independently of the mGBA base pin.
The vendored copy normalizes one trailing space from the canonical source so
repository whitespace checks remain clean. Vendored SHA-256:
`61bdfdfbf4c07f751323e44ba3580ea8d978786e62e80c446502efc70ae326f7`.

## Linux regression baseline

Baseline commands, tool versions, and results are recorded here before and
after the header refresh.

### Unmodified upstream source

- Host: Fedora Linux 44 Toolbx, x86-64
- Compiler: GCC `16.1.1 20260515 (Red Hat 16.1.1-2)`
- Build tool: CMake `4.1.2` from a temporary Python target directory
- Test dependency: CMocka `1.1.7` (`a01cc69ee9536f90e57c61a198f2d1944d3d4313`)
  built into `/tmp/mgba-deps`
- Source state: mGBA source files match
  `1d65391d3531f9338300f306c4e1d76c258ce657`; only planning files and
  `UPSTREAM.md` differ

Configuration intentionally disables optional host libraries unavailable in
the minimal container. The GBA/GB cores, shared mGBA library, libretro core,
fuzz harnesses, and CMocka suite remain enabled.

```sh
env PYTHONPATH=/tmp/mgba-build-tools \
    PKG_CONFIG_PATH=/tmp/mgba-deps/lib64/pkgconfig \
    /tmp/mgba-build-tools/bin/cmake \
    -S . -B build-baseline-tests-pkg \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_DISABLE_FIND_PACKAGE_cmocka=TRUE \
    -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_LIBRETRO=ON \
    -DBUILD_TEST=ON -DBUILD_SUITE=ON -DBUILD_CINEMA=OFF \
    -DBUILD_HEADLESS=OFF -DBUILD_EXAMPLE=OFF \
    -DUSE_ZLIB=OFF -DUSE_MINIZIP=OFF -DUSE_PNG=OFF \
    -DUSE_LIBZIP=OFF -DUSE_SQLITE3=OFF -DUSE_LZMA=OFF \
    -DUSE_JSON_C=OFF -DUSE_FREETYPE=OFF -DUSE_FFMPEG=OFF \
    -DUSE_ELF=OFF -DUSE_LUA=OFF -DENABLE_SCRIPTING=OFF \
    -DUSE_DISCORD_RPC=OFF \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

env PYTHONPATH=/tmp/mgba-build-tools \
    PKG_CONFIG_PATH=/tmp/mgba-deps/lib64/pkgconfig \
    /tmp/mgba-build-tools/bin/cmake \
    --build build-baseline-tests-pkg --parallel 8

env PYTHONPATH=/tmp/mgba-build-tools \
    LD_LIBRARY_PATH=/tmp/mgba-deps/lib64 \
    /tmp/mgba-build-tools/bin/ctest \
    --test-dir build-baseline-tests-pkg --output-on-failure
```

Results:

- Build: pass, including `build-baseline-tests-pkg/mgba_libretro.so`
- Export check: `retro_init`, `retro_get_system_info`, and `retro_load_game`
  are exported
- Tests: 20 passed, 1 failed, 21 total
- Known baseline failure: `util-hash` / `stagedCrc32` at
  `src/util/test/hash.c:31`. GCC 16 warns that the test's `'\n\n'`
  multi-character constant overflows the one-byte initializer to `'\n'`; the
  resulting CRC is `0xc33133d7`, while the test expects `0x09304ebd`.
- Total CTest time: 0.05 seconds

This existing failure is retained as a baseline observation. Netplay work must
not add failures beyond it, and tests directly touched by the work must pass.

### Refreshed canonical header

The canonical header was fetched by immutable revision, its SHA-256 was checked,
and it was copied to the vendored destination. One trailing space in a
documentation comment was normalized without changing C tokens:

```sh
curl -fsSL \
  https://raw.githubusercontent.com/libretro/RetroArch/556283a6689ab5502ceec86f4e83e8b8d796bbd8/libretro-common/include/libretro.h \
  -o /tmp/libretro-556283a6689ab5502ceec86f4e83e8b8d796bbd8.h
sha256sum /tmp/libretro-556283a6689ab5502ceec86f4e83e8b8d796bbd8.h
cp /tmp/libretro-556283a6689ab5502ceec86f4e83e8b8d796bbd8.h \
  src/platform/libretro/libretro.h
```

The refreshed file defines `RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE` as
command 78, reliable ordered delivery, flush hints, broadcast ID `0xFFFF`, the
current callback types, and `struct retro_netpacket_callback`.

The same build and CTest commands from the unmodified-source baseline were run
again. The core rebuilt successfully and the test result remained exactly 20
passed / 1 known baseline failure.

Load probe:

```sh
python3 -c 'import ctypes, pathlib; p=pathlib.Path("build-baseline-tests-pkg/mgba_libretro.so").resolve(); c=ctypes.CDLL(str(p)); c.retro_api_version.restype=ctypes.c_uint; print("api_version=%d" % c.retro_api_version()); print("dlopen_and_symbol_call=pass")'
```

Result:

```text
api_version=1
dlopen_and_symbol_call=pass
```

## Feature qualification

The completed change preserves the pinned upstream ancestry and header revision
above. Detailed automated, sanitizer, localhost, two-device Android, and
independent-workload evidence is recorded in
`docs/netplay-validation-matrix.md`; build, installation, protocol, policy, and
failure semantics are documented in `docs/wifi-link-netplay.md`.

After splitting and rebasing the upstream-facing stack onto
`71aa6c7dab7654bfdbbd57e696f704671a97e55d`, a clean Linux build produced the
shared library, libretro core, and all test executables. The complete normal
suite passed 28 of 29 tests; its only failure remains the same
`util-hash/stagedCrc32` baseline case documented above. The focused normal and
ASan/UBSan netplay/SIO/libretro suites each passed 8 of 8 tests, and Arm GNU
Toolchain 15.2.Rel1 reproduced the committed 32 KiB test ROM byte-for-byte.

Android NDK r27 builds passed for `arm64-v8a`, `armeabi-v7a`, `x86`, and
`x86_64`; the ARM64 production core was validated on the two physical Android
devices described in the validation matrix.
