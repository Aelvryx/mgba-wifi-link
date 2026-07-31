# Replicated-pair scheduler spike

Date: 2026-07-31

Decision: use cooperative single-thread scheduling for the protocol-v2 replicated pair. Keep the two-worker implementation only as comparative test evidence.

## Test construction

`test-gba-replicated-pair-spike` creates two independent GBA cores from the same effective ROM, gives each a separate memory-backed save, assigns deterministic logical IDs P0 then P1, and attaches both existing `GBASIOLockstepDriver` instances to a fresh `GBASIOLockstepCoordinator`.

The CC0 continuous fixture repeats the validated two-player MULTI transfer matrix at every baud rate. Its SHA-256 is `5d82dc1f4c62c99cf024c6978671fce4dbb23671658f4bfc8e8e52a124802e9d`. Each scheduler runs 600 frames per logical core, slightly more than ten seconds of emulated GBA time, with no frontend callbacks from either emulation context.

Run the normal comparison with:

```sh
cmake --build build-ci-review --target test-gba-replicated-pair-spike
build-ci-review/test/test-gba-replicated-pair-spike
```

## Results

Representative normal results on the development Linux host were:

| Scheduler | Frames | Valid transfers | Wait callbacks P0/P1 | Wall time | Approximate paired real-time multiple |
| --- | ---: | ---: | ---: | ---: | ---: |
| Cooperative | 600 / 600 | 619 / 619 | 1,561 / 12,818 | 174–194 ms | 52–58x |
| Two worker | 600–601 / 600 | 620 / 620 | 1,563–1,564 / variable | 99–110 ms | 91–101x |

Both candidates preserved words, player IDs, busy clearing, serial IRQs, and every fixture assertion. The cooperative candidate produced exactly the same per-frame trace hashes, run-loop count, transfer counts, and wait counts on every repeated run:

```text
P0 7d8079e8622402b4
P1 a62070af7327edbc
run loops 942419
transfers 619 / 619
```

The two-worker candidate was faster, but its frame-boundary trace hashes, secondary wait count, and occasional final-frame overshoot varied with host thread interleaving. That nondeterminism is an unnecessary liability for replicated execution, where both physical devices must reproduce the same state from frame-numbered input.

## Teardown and sanitizer evidence

Focused tests tear the cooperative pair down during partial construction, idle execution, a mode barrier, transfer start, pending transfer completion, and reset. Normal and ASan/UBSan runs pass with leak detection enabled.

ThreadSanitizer initially identified an existing `mCoreThreadEnd()` race: `sync.audioWait` was read under the thread-state mutex but written under the audio mutex. The read now uses the audio mutex, and the complete pair spike passes with `TSAN_OPTIONS=halt_on_error=1`.

## Rationale

Cooperative scheduling is selected because it is deterministic, comfortably exceeds the performance gate on the development host, uses the lockstep driver's existing sleep/wake contract directly at mTiming boundaries, avoids worker lifetime and callback-routing complexity inside libretro, and tears down synchronously. The worker candidate's roughly 1.8x host throughput advantage is not worth nondeterministic replica boundaries.

The subsequent exact ARM64 diagnostic gate passed on both target devices. See
[`replicated-pair-android-feasibility.md`](replicated-pair-android-feasibility.md)
for the reproducible artifact, 10-minute soak evidence, thermal measurements,
and GO decision.
