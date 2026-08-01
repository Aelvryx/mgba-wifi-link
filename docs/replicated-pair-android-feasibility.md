# Replicated-pair Android feasibility gate

Date: 2026-07-31

Decision: **GO**. The cooperative replicated-pair architecture clears the
Android feasibility gate on both target devices. Production protocol-v2 work
may proceed.

## Exact diagnostic build

The tested source snapshot was based on
`380fe9a8b1c9807c1d72f1ddae5f21bf2fd8f875`, with Git tree object
`63d5ac7a6955be1de8a8b2cafeae49c20ffee1ac` and staged binary-diff SHA-256
`874f86cc2eb7f3849b3c785f1b5c11dc48d2b7f44e13f03d4c2fb8e521267971`.
The temporary diagnostic option was disabled by default and did not alter the
protocol-v1 runtime unless explicitly enabled before content load. It was
removed from the release core after this gate passed; equivalent local-pair
coverage remains in the focused test targets.

Two clean ARM64 Release build directories used Android NDK r27
(`27.0.12077973`), Clang 18.0.1, API 21, and the same CMake inputs. Removing
debug sections and the build-ID note produced byte-identical artifacts:

```text
b3057db285df47a2127c36d4f6e2be4c3f689ce47ae365be91f2f727c1a7de5d
```

The installed file was 3,288,456 bytes and was named
`mgba_replicated_pair_diagnostic_libretro_android.so`, leaving the normal mGBA
core installed alongside it.

The continuous fixture SHA-256 was:

```text
5d82dc1f4c62c99cf024c6978671fce4dbb23671658f4bfc8e8e52a124802e9d
```

## Isolation

Both RetroArch 1.22.2 installations loaded the diagnostic core through a
dedicated configuration. Core options, logs, saves, and states were redirected
under the external-storage `mgba-replicated-pair-diagnostic` directory. The
normal RetroArch configuration and the normal save/state file inventories were
hashed before and after the run and remained unchanged on both devices.

Audio used OpenSL with synchronization enabled. Video used the normal GL driver
at 1920x1080 with threaded video disabled and vertical synchronization enabled.
The test therefore includes ordinary Android frontend presentation and audio
work rather than using null drivers.

## Results

The formal ADB collectors obtained 61 samples per device at nominal ten-second
intervals. ADB collection overhead extended the observed windows to 760 seconds
on Thor and 774 seconds on Odin, both exceeding the required ten minutes. The
core logs span 814.5 seconds and 49,080 paired frames because execution began
before sampling and continued while evidence was copied.

| Metric | AYN Thor | AYN Odin2 Portal |
| --- | ---: | ---: |
| Paired frames | 49,080 / 49,080 | 49,080 / 49,080 |
| Valid MULTI transfers | 51,152 | 51,152 |
| Minimum reported wall FPS | 60.255 | 60.255 |
| Mean reported wall FPS | 60.282 | 60.281 |
| Final reported wall FPS | 60.256 | 60.256 |
| Final serial words per emulated second | 124.498 | 124.498 |
| Mean paired-core work per frame | 2.124 ms | 4.284 ms |
| Maximum paired-core work for one frame | 4.195 ms | 7.025 ms |
| Process CPU mean / p95 / max | 18.8% / 23.0% / 23.0% | 35.8% / 38.4% / 42.3% |
| Peak resident memory | 133,152 KiB | 142,612 KiB |
| Hottest CPU/GPU sensor p50 / p95 / max | 82.8 / 84.4 / 84.4 C | 72.5 / 74.5 / 75.3 C |
| Maximum battery temperature | 30.0 C | 30.0 C |
| Android thermal status | 0 in all 61 samples | 0 in all 61 samples |
| Forced process-stop acknowledgement | 91 ms | 97 ms |

The hottest Thor CPU sensor is noteworthy, but Android never raised its thermal
status, the prime CPU policy continued to advertise and use its maximum
frequency, memory remained flat, and neither emulation rate nor paired-core
runtime degraded. Odin likewise reported thermal status zero throughout. The
different non-prime policy frequencies were stable governor choices, not a
temperature-correlated reduction.

No fatal signal, Java fatal exception, assertion, replicated-pair stall, or
core-reported error occurred. Every periodic P0/P1 frame count matched. The
complete diagnostic counter stream was identical between devices, including
transfer counts, run-loop counts, and lockstep wait counts.

## Baseline comparison

The Linux cooperative scheduler baseline reached exactly 600/600 frames,
619/619 transfers, 942,419 run loops, and 1,561/12,818 wait callbacks. Each
Android device reported those exact values at frame 600. Since serial throughput
is measured per emulated second, Android has a zero-percent gap from the Linux
local-lockstep baseline at the same frame boundary. The longer Android runs
settled at 124.498 words per emulated second while continuing to sustain more
than 59 wall frames per second.

The two-worker candidate remains faster on desktop but is not selected: its
state traces vary with thread interleaving. Android results show no performance
need to accept that nondeterminism.

## Evidence hashes

Raw evidence remains in the ignored diagnostic build directory. Its hashes are
recorded here so copied evidence can be checked without committing device logs:

| Evidence | SHA-256 |
| --- | --- |
| Thor core log | `6ab33c3f12a52bec32c1967e0e0adcc9ba40e39eed0bf9484a00d64de8f9143b` |
| Odin core log | `486c1bdc8d63d506dc4c740e9d2dfda6bded91f5cbf0d9d1ed8567eca57959d5` |
| Thor ADB soak samples | `ee6ed397cac6614133d7495df3c4ee0cc8699ab112f135a43b23a86c03024369` |
| Odin ADB soak samples | `1ddd674d89358c6e9e26f1b402153d990b4e7a13376664eb1610c85d710d6c89` |
| Thor logcat | `cfe8b595fcf7cc8ee3b9c7e52a33df753cd4424ca4f2597f5a5a09cdbbb9bd26` |
| Odin logcat | `798a4ed95183b13da21fe8ad89938fe6b30956beeea2be6ad2f24a41eb710dd8` |

The direct libretro pair suite also passes in normal and ASan/UBSan builds with
leak detection enabled. The transport-independent scheduler suite continues to
pass its normal, ASan/UBSan, and ThreadSanitizer gates.
