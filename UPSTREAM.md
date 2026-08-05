# Upstream and Patch-Stack Record

## mGBA base

- Upstream repository: `https://github.com/mgba-emu/mgba.git`
- Upstream source branch: `master`
- Pinned upstream commit: `71aa6c7dab7654bfdbbd57e696f704671a97e55d`
- Pinned commit date: `2026-07-30T23:01:50-07:00`
- Pinned commit subject: `Qt: Remove unneeded Qt Multimedia-only include`
- Fork repository: `https://github.com/Aelvryx/mgba-wifi-link.git`
- Protected integration branch: `master`

The repository was initialized directly from mGBA Git history. The
fork's `master` patch stack descends from the pinned commit above and contains
the reviewed GBA Wi-Fi Link product, tests, CI, user documentation, and
archived OpenSpec decisions. Feature branches are short-lived review branches
created from protected `master`; none is a permanent alternate runtime or
provenance line. Project-local `.codex` helpers, device captures, logs,
commercial ROMs, and generated build products remain outside this patch stack.

```sh
git fetch upstream master --tags
git switch master
git pull --ff-only origin master
git switch -c feature/<reviewed-change>
```

No mGBA source snapshot was imported and no upstream commit was copied into a
new root. Future refreshes must rebase this patch stack onto an explicitly
selected upstream commit, update the pin above, and rerun the recorded baseline
and feature tests.

## Patch-stack conventions

- Keep upstream-generic SIO dispatch and hardware-characterisation changes in
  focused commits that can be reviewed independently.
- Keep the protocol-v2 codec/session logic separate from the libretro
  Netpacket adapter and local replicated-pair cable execution. The retired
  distributed-cable protocol is preserved only in labelled historical
  evidence and Git history.
- Keep feasibility experiments out of production build paths after their
  findings and reusable scheduler/frontend lifecycle tests have graduated to
  permanent-purpose ownership.
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

Baseline commands, tool versions, and results from the original implementation
base are retained here to make the header refresh auditable. Final validation
against the current pinned base is recorded under Feature qualification.

### Original unmodified upstream source

- Host: Fedora Linux 44 Toolbx, x86-64
- Compiler: GCC `16.1.1 20260515 (Red Hat 16.1.1-2)`
- Build tool: CMake `4.1.2` from a temporary Python target directory
- Test dependency: CMocka `1.1.7` (`a01cc69ee9536f90e57c61a198f2d1944d3d4313`)
  built into `/tmp/mgba-deps`
- Source state: original mGBA base
  `1d65391d3531f9338300f306c4e1d76c258ce657`

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

## GBA Wi-Fi Link qualification

GBA Wi-Fi Link preserves the pinned upstream ancestry and header revision
above. Its concrete session and wire compatibility contract remains version 2.
Detailed automated, sanitizer, localhost, two-device Android, and
independent-workload evidence is recorded in
`docs/gba-wifi-link-validation-matrix.md`; build, installation, protocol,
policy, and failure semantics are documented in `docs/gba-wifi-link.md`.

After splitting and rebasing the first upstream-facing stack onto
`71aa6c7dab7654bfdbbd57e696f704671a97e55d`, a clean Linux build produced the
shared library, libretro core, and all test executables. The complete normal
suite passed 37 of 38 tests; its only failure remains the same
`util-hash/stagedCrc32` baseline case documented above. The protocol-v2
development stack reached 17 focused tests before the retired v1-only targets
were removed. The integrated product now has 13 focused executables; all 13
pass normally, under ASan/UBSan with leak detection, and under TSan. Its
real-adapter replay and 134,400-frame stock RetroArch soak are recorded in the
validation matrix. Arm GNU Toolchain
15.2.Rel1 reproduces the committed 32 KiB test ROM byte-for-byte.

The feature workflow also configures and runs the complete normal suite while
checking the pinned `util-hash/stagedCrc32` baseline independently. A separate
Android job builds and inspects the arm64-v8a libretro core with NDK r27, so
common-core and Android ABI regressions do not depend solely on focused tests
or physical-device qualification.

Android NDK r27 builds passed for `arm64-v8a`, `armeabi-v7a`, `x86`, and
`x86_64`. The current post-review ARM64 release candidate was built from
source commit `c9b181aa24d5f2136a6e11fca56179b5204555be` (tree
`c4ecab88bd6270ad1884f6629e3f2dbe6a15983e`). The 8,043,736-byte core has
SHA-256
`14978106a3978ab4ef6ec025add82e0e9a38decda72404e3dc8c02b3373f179d`.
Its embedded version and commit strings were verified after installation on
both physical Android devices. The exact-head continuous smoke sustained 60
FPS with matching P0/P1 traces, normal audio delivery, zero serial errors or
timeouts, and atomic verified-checkpoint restoration after forced peer stop.
The same binary passed a 15,600-frame Mario Kart Multi-Pak gameplay smoke with
13,100 local cable words and no audiovisual or protocol fault. Detailed hashes
and observations are in `docs/gba-wifi-link-validation-matrix.md`.

The earlier 8,029,120-byte production candidate from
`9c528d38965998b15c8e7325326fd96f74362088` had SHA-256
`9cbdf6adc49ada15fc670bea3e4c2bad64803fe7d5d749d3143e98773a0a14f8`
and supplied the terminal-path evidence retained in the validation matrix.

The earlier exact ARM64 candidate from
`217c231f2b1152b9e7b8484b0245f2210ae709d0`, SHA-256
`e045d614e5b2def408ee636c7cbffe46e94105edcb8abda65c8142879cca2989`,
completed the 120,600-frame continuous-link qualification recorded there.
The later production commit removes completed feasibility switches without
changing the qualified replicated scheduler or wire protocol.
