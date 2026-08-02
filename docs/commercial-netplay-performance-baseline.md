# Commercial Netplay Performance Baseline

Date: 2026-07-31

Status: retired protocol-v1 historical evidence. The implementation and its
packet-log analyzer are no longer in the active source tree; Git history
preserves both. This document remains the architectural regression baseline
for `make-wifi-link-netplay-realtime`.

## Scope and privacy

Two stock RetroArch 1.22.2 Android frontends ran the approved ARM64 core from upstream-facing implementation head `a5a215929fbd5dffceaba2b1be9fc8f520e346bc` (published core SHA-256 `c319602f9aebb65ebd74af7c417e6b837ff4ffdabda8235d82ce4372c5990432`) on an AYN Thor and an AYN Odin2 Portal over the same Wi-Fi LAN.

The captures used isolated diagnostic configurations and save directories. Normal RetroArch configuration and user saves were not modified. Commercial ROM content, screenshots, raw saves, device serials, IP addresses, and raw Android dumps are not committed. This report retains only derived measurements and hashes for the four core logs:

| Capture | SHA-256 |
| --- | --- |
| Run 1 Thor core log | `5c0dec1daa22b1c26fd3be0406d5d488923491c4773d63b7373b3c619e7414cf` |
| Run 1 Odin core log | `d4c12dd94d19c10d0f61c420a73b21f09ca6f74033d4b692c828aff03db133c3` |
| Run 2 Thor core log | `c61fbe4f394089cb33959745de479da1ebc56476936587cf9fcdfe59c9671741` |
| Run 2 Odin core log | `1a51eb60f84dbb2518f248598201e6d48df8450f52a19d3f846e412a5d256dfa` |

At the time of capture, metrics were reproduced from equivalent traces with
the now-retired analyzer:

```sh
tools/analyze-link-netplay-log.py retroarch.log \
  --ping ping.txt --top top.txt --logcat logcat.txt --pretty
```

The analyzer reports printed trace lines separately from packet totals inferred from authoritative per-sender sequences. Grant trace sampling intentionally hides later `EXECUTION_GRANT` and `GRANT_ACK` lines, so counting log lines is not packet accounting.

## Finding 1: connection alone serializes frame progress

The host grants exactly 280,896 GBA cycles, one nominal video frame, then waits for player one's acknowledgement before issuing another grant. The clean Run 2 sample measured:

| Metric | Result |
| --- | ---: |
| Sampled grants | 8 |
| Grant/ack RTT, min / median / mean / p95 / max | 24 / 28.5 / 28.75 / 33.6 / 35 ms |
| Grant-send interval, min / median / mean / p95 / max | 29 / 32 / 33.86 / 40.9 / 43 ms |
| Horizon step | 280,896 cycles for every sample |
| Inferred real emulated rate | 29.54 frames/s |

Run 1's noisier sample inferred 22.58 emulated frames/s. RetroArch's on-screen counter could still report approximately 120 FPS because blocked `retro_run()` calls returned cached video without advancing `core->runFrame()`. That frontend-call rate is not the GBA's emulated-frame rate.

The causal path is:

```text
host runs one frame
  -> sends one horizon
  -> later frontend callbacks return blocked/empty
  -> client eventually runs to the horizon and acknowledges
  -> host is released for one more frame
```

The two devices therefore compute sequentially across Wi-Fi. A bounded receive rendezvous inside the active `retro_run()` call prevents repeated empty returns and the associated audio starvation, but it cannot make the host-led simulation concurrent: player one cannot run a frame until player zero has reached and granted that horizon. At the measured 28.75 ms mean grant round trip, the RTT-only upper bound is approximately 34.78 emulated frames per second before either device's emulation work is counted. The observed 29.54 frames per second is consistent with that bound. The rendezvous is therefore a correctness and pacing improvement for protocol v1, not its real-time performance fix.

## Finding 2: empty blocked calls starve Android audio

Android logcat showed an `AudioTrack` stop/restart storm beginning with Netpacket play:

| Device | Stop calls in storm cluster | Cluster span | Calls/s | Median frames delivered | p95 frames | Calls at or below 1,024 frames |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Thor | 15,944 | 284.807 s | 55.98 | 384 | 576 | 15,942 |
| Odin | 16,055 | 375.038 s | 42.81 | 384 | 576 | 16,053 |

Before the cluster, each capture contained only one old stop separated by a long idle gap. The near-frame-rate stream of tiny deliveries is consistent with the core returning cached video while no new GBA audio was generated. User-visible output was heavily stuttered and pitch/tempo warped immediately after connection.

Run 1 process samples averaged 40.92% CPU on Thor and 40.29% on Odin, with p95 values of 43% and 42%. The failure was not CPU saturation.

## Finding 3: a successful Four Swords transaction is still far too slow

Run 2 entered the Four Swords linking screen. No timeout, malformed packet, protocol failure, or SIO error occurred. The session completed 2,803 successful 16-bit serial-word transactions before it was deliberately stopped; one final START remained incomplete at capture end.

| Phase | Samples | Min | Median | Mean | p95 | Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| START to READY | 2,803 | 5 ms | 16 ms | 21.68 ms | 68 ms | 128 ms |
| CATCHUP to completion READY | 2,803 | 6 ms | 16 ms | 24.49 ms | 86 ms | 123 ms |
| DECISION to final ACK | 2,803 | 4 ms | 10 ms | 10.31 ms | 13 ms | 58 ms |
| Complete START to final ACK | 2,803 | 25 ms | 44 ms | 56.54 ms | 118 ms | 160 ms |
| Consecutive START interval | 2,803 | 25 ms | 45 ms | 59.87 ms | 124 ms | 184 ms |

The 2,804 STARTs span 167.805 seconds, for only 16.704 completed words per second. The game consequently fell to roughly one visible frame per second while it continuously exchanged link data.

The healthy transaction is approximately three network round trips per word:

```text
START -> READY
COMMIT / COMPLETION_CATCHUP -> COMPLETION_READY
COMPLETION_DECISION -> COMPLETION_DECISION_ACK
```

Combining messages can reduce the constant but cannot make a dependent word stream approach the emulated cable rate. Even one Wi-Fi round trip per word remains orders of magnitude too slow.

## Finding 4: sampled traces understate actual traffic

The Run 2 host log printed 19,647 packet trace lines carrying 1,246,136 application payload bytes. The complete sender sequence domains ended at host sequence 28,352 and client sequence 25,547. They began at one and the clean run had no intentional allocation holes, so the session actually emitted 53,899 application packets.

Grant sampling suppressed 17,126 lines in each direction. Accounting for those known 48-byte messages gives approximately 2,890,232 application payload bytes before Netpacket and lower-layer overhead. Printed trace volume and protocol traffic are therefore:

| Accounting | Packets/lines | Payload bytes |
| --- | ---: | ---: |
| Printed after sampling | 19,647 | 1,246,136 |
| Inferred from sender sequences | 53,899 | 2,890,232 |

During the active transaction stream alone, every word produced seven application messages. Protocol v2 must make ordinary network traffic proportional to video frames, not serial words.

## Finding 5: LAN quality was adequate

Concurrent ICMP captures were stable:

| Device | Samples | Mean | Median | p95 | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| Thor | 925 | 11.12 ms | 10.8 ms | 14.8 ms | 36.1 ms |
| Odin | 927 | 11.35 ms | 10.9 ms | 15.2 ms | 40.2 ms |

These values explain the observed barriers but do not indicate a broken Wi-Fi link. The implementation makes ordinary frame progress and every serial word depend on latency that is normal for the target network.

## Acceptance baseline

The replacement is successful only when the exact two-device build demonstrates all of the following:

- at least 59 actual emulated/displayed frames per second while connected and during continuous MULTI traffic;
- no recurring blocked frontend calls that produce no new audio;
- serial throughput within 5% of the same build's local two-core lockstep baseline;
- normal traffic proportional to frames with no per-word protocol-v1 transfer messages;
- matching deterministic replica checks for a 30-minute transfer-heavy run;
- usable controls, audio, multiplayer entry, gameplay, and teardown in at least one fast-entry commercial Multi-Pak title;
- an honest compatibility result for every attempted title, with Four Swords retained as a named investigation rather than counted as a successful link;
- recorded input delay, rendezvous percentiles, CPU, memory, temperature, and throttling state.

The retired implementation was valuable as a deterministic SIO and
failure-semantics oracle, but it was not a viable commercial-game runtime.
Current correctness evidence belongs to the common SIO, replicated-pair, v2
session, adapter, and paired-replay suites.
