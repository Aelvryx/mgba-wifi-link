# GBA Link Netplay Validation Matrix

Date: 2026-07-31

This document maps the MVP's automated evidence to protocol phases and failure
classes. Test names below are cmocka case names inside the named source file.

## Protocol-v2 real-time evidence

Protocol v2 replaces per-cable-event network barriers with one replicated
P0/P1 pair on each endpoint and frame-authoritative input packets. The current
integration branch has the following automated and rendered-device evidence:

- All 17 focused SIO, input-ring, v2 codec/session, replica, pair,
  save-routing, and libretro-adapter test executables pass normally, under
  ASan/UBSan with leak detection, and under TSan.
- `test-libretro-netpacket-v2-replay` compiles the actual adapter twice into
  one process and connects the independent host/client instances through an
  ordered deterministic-latency wire. It covers a complete bilateral replica
  exchange, 125 input frames, state checks, role presentation, every
  attachment packet boundary, runtime input/check loss, detach, synchronous
  stop, reset, and unload.
- `canonicalDigestsCoverFutureStateOnly` detects injected one-bit P0 memory,
  P1 memory, timing, and save changes while ignoring presentation-buffer
  changes.
- `saveBackingBelongsOnlyToAssignedPlayer` proves host/P0 and client/P1 save
  ownership without exposing the shadow save to the frontend buffer.
- `runtimeInputDeadlineFailsWithSpecificReason` and
  `missingPollingAndSynchronousStopFailClosed` cover bounded input wait and
  generation invalidation during a receive callback.
- A stock-RetroArch localhost soak completed 134,400 replicated frames, or
  37 minutes 20 seconds of emulated time. All 448 rolling trace digests (224
  sampled frames times P0/P1) matched between endpoints, and 2,239 periodic
  verification rounds completed on each endpoint without divergence.
- The soak completed 124,680 generic MULTI transfers and 249,360 cable words.
  Its 0.927679 transfers per replicated frame differed by only 0.002% from a
  direct local-pair baseline of 100,187 transfers over 108,000 frames.
- The host/client sent 136,680/136,679 application packets at the final
  periodic sample: approximately 1.017 packets per endpoint per replicated
  frame, independent of the much larger serial-word count.
- Both logical cores produced audio for every completed frame (the periodic
  log observes 134,399 presented frames immediately before frame 134,400) and
  both endpoints recorded zero empty-audio frames. Copied-queue high-water
  marks remained 34/32 packets and the maximum in-call rendezvous was 7/4 ms
  on this localhost run.
- Normal max-frame shutdown restored each retained single core to frame
  134,400, the latest jointly verified quiescent state, while retaining its
  live local save generation.

The short structured summaries include cumulative packets/bytes, verification
count, local SIO transfers/words/waits, input rendezvous timing, future input
depth, queue high-water, audio samples/frames/empties, per-core scheduler work,
and separate full rolling P0/P1 trace digests. The checked
`tools/analyze-replicated-netplay.py` validator compares the two logs and the
direct-pair baseline, enforcing the frame floor, every sampled trace, serial
counters, state-check coverage, audio coverage, packet scaling, queue bound,
and five-percent throughput limit. The soak used core SHA-256
`62edfb5504f66e3ece28f18ed6418d4ae401ef24841abdacc61496d622d4be5d`
and cartridge-sized continuous fixture SHA-256
`c302487462e6f1241e038fab8b135a43909cfce9f07cfe7498a2b1fc7b4c8330`.
The exact ARM64 physical-device qualification is recorded below.

### Mario Kart Multi-Pak commercial qualification

A human-assisted two-device run used stock RetroArch 1.22.2 on an AYN Thor
host and AYN Odin2 Portal client over the same Wi-Fi LAN. Both devices loaded
the same effective Mario Kart: Super Circuit image (RetroArch content CRC32
`ed316e37`) from isolated qualification paths, connected through protocol v2,
entered Multi-Pak, selected a two-player VS race, chose separate characters,
and completed all three laps. The captured round-one results ranked P1 first
and P2 second on both endpoints.

The installed 8,067,160-byte ARM64 candidate had SHA-256
`54e37896117762430efad018a51c38a69153ecf93974a931794d9096ff35db21`.
It was built from integration base `6bfd68439e199fb5686d399748e8328caf4a8f51`
plus the production scheduler/diagnostic changes subsequently committed
unchanged as `525ff8644`. The qualification capture also recorded the dirty
worktree's binary Git diff SHA-256 as
`67998b9e7ba943f9fac878b493279dc29ce0dd989ac91f6bed1547e3999ed7eb`.
Together the commit and captured build hash identify the physically exercised
candidate while keeping commercial content outside the repository. A
post-commit exact-build smoke is the final release gate.

The strict commercial-log analyzer result was:

```text
frames=31200 checks=519/519 packets=31756/31755
serial=45127/90254 audio_empty=0/0 fps=60.234/60.237
rv_p50=5/8ms rv_p95=13/23ms rv_max=84/30ms
packet_rate=61.308/61.309pps byte_rate=7765.1/7765.3Bps
lead=1/1 trace_samples=52
```

The session ran for 517.98/517.95 seconds at the last common sample. All 52
P0 and all 52 P1 rolling state digests matched between endpoints. Cable
traffic began only when the game entered Multi-Pak: the frame-8,400 sample had
zero transfers and frame 9,000 had 1,326. The final sample contained 45,127
completed two-player MULTI transactions and 90,254 transferred words. Both
logical cores remained in MULTI mode throughout gameplay. Each endpoint
reported audio for 31,199 of the 31,200 sampled frames and zero empty-audio
frames. No link failure, protocol error, timeout, divergence, mismatch, or
disconnect appeared in either log.

The host measured an 82 ms attachment round trip and selected a fixed
five-frame input delay. Runtime input rendezvous was 5/13/84 ms p50/p95/max on
Thor and 8/23/30 ms on Odin. One bounded scheduler lead was recovered for each
logical core; the lead counters then remained stable through the race. The
human players completed the race normally without reporting an audiovisual or
control fault.

Raw logs and screenshots remain outside the repository. Their verification
hashes are:

| Evidence | SHA-256 |
| --- | --- |
| Thor result screenshot | `51c9a8f3febc09504f692dfdb2a8df46d6663f576060f77026e54c5900f48bf6` |
| Odin result screenshot | `571cc37bcf932fc7fbdc8582bc64c9d9fd3241fcca0e9b0398fa43b9fa8c03a7` |
| Thor RetroArch/core log | `64ae30a83cea624cd16bfdbcb93c55de667692059ff7a84ad13888d6dadef376` |
| Odin RetroArch/core log | `aa4deaea89320ba8eb4d4a93cd0e0136ff6da4349596f8b5dde8be1d03c36a07` |

The analyzer command for a real-game run is:

```sh
python3 tools/analyze-replicated-netplay.py \
  /path/to/host.log /path/to/client.log \
  --minimum-frames 30000 --commercial
```

### Exact-build Android continuous-link qualification

The ARM64 candidate built from source head
`217c231f2b1152b9e7b8484b0245f2210ae709d0` (tree
`a1cca0be9434d3beeac79788fbfa745845381d76`) with Android NDK r27 was installed
through stock RetroArch 1.22.2 on the AYN Thor host and AYN Odin2 Portal
client. The installed 8,063,296-byte core had SHA-256
`e045d614e5b2def408ee636c7cbffe46e94105edcb8abda65c8142879cca2989`.
The isolated, black-screen qualification ROM had SHA-256
`09580c39920cafa6f597f20a9019098260442242fbdbd7402e15d1217484abe3`.

The continuous rendered run exceeded 33 minutes and completed 120,600 common
replicated frames. The strict analyzer result was:

```text
frames=120600 checks=2010/2009 packets=122650/122649
serial=60297/120594 audio_empty=0/0 fps=60.219/60.220
rv_p50=21/21ms rv_p95=53/46ms rv_max=251/223ms
packet_rate=61.243/61.244pps byte_rate=7130.2/7130.3Bps
trace_samples=201 baseline_delta=0.008%
```

All 201 sampled P0/P1 trace pairs matched. The fixture reported RUNNING on
both logical players, 60,295 matching measured transfers per endpoint, and
zero fixture errors or timeouts. The two extra local SIO transfers are fixture
synchronization transactions. Every completed frame supplied ordinary
local-role audio and neither endpoint returned an empty audio frame. The
negotiated input delay was four frames. Ordinary traffic remained about one
packet per endpoint per frame rather than scaling with the much larger serial
word count.

The direct frame-paced pair baseline produced 59.719 serial words per emulated
second. The rendered Android run differed by only 0.008%, well inside the
five-percent gate. ADB sampling caused isolated rendezvous maxima of 251 ms on
Thor and 223 ms on Odin without causing a timeout, divergence, or sustained
frame-rate reduction.

| Metric | AYN Thor | AYN Odin2 Portal |
| --- | ---: | ---: |
| ADB samples / observed collector window | 61 / 1,971 s | 61 / 1,992 s |
| Process CPU mean / p95 / max | 33.31% / 38.40% / 50.00% | 48.72% / 57.60% / 61.50% |
| Peak resident memory | 153,232 KiB | 155,828 KiB |
| Hottest CPU/GPU sensor p50 / p95 / max | 50.3 / 57.4 / 59.4 C | 40.0 / 43.2 / 50.7 C |
| Maximum battery temperature | 28.0 C | 27.0 C |
| Android thermal status | 0 in all samples | 0 in all samples |

The run used isolated configs, options, logs, saves, and states. The normal
RetroArch configs, normal mGBA options, and canonical user-save manifests were
hashed before and after qualification and were byte-identical:

| Preserved data | Before and after SHA-256 |
| --- | --- |
| Thor RetroArch config | `eb3a81dd57e247715c71e76ade295080c1a45a9e7830d311568b3ac1d1365c24` |
| Odin RetroArch config | `f7b9545632423876000627d3181db095700de2d1ed941ce11ca5fdbe6b36d7f1` |
| Thor/Odin normal mGBA options | `65fd9076a4f1cd6f4aa09ad3965bab83c00ea4b7a90c0626674737d7f1f6a468` |
| Thor user-save manifest | `a7017a805b801f60f7bafcf97ea7e0ce52bd0a1afa6b72c4909d7d4344dadcc6` |
| Odin user-save manifest | `65d2e3196cd7f4c36454855b8ee01805aa9643fddeabcc2c7f8db5678a6f86fb` |

Raw logs remain outside the repository and contain no committed ROM or save
data. Their verification hashes are:

| Evidence | SHA-256 |
| --- | --- |
| Thor core log | `46ddfe4bbe4ebbabdbb0252e6264e1b5d8711f98afb9a2757edb3ba4782d9bf2` |
| Odin core log | `94d975ad9baa6a3e4d9aab501ac589f7d21311db3dc3de599265c3ec149072c8` |
| Thor ADB samples | `f52f082685e55b27faf632ac65b2c548fba1c963176808cfb80d2eff0e4b161c` |
| Odin ADB samples | `f2bc69474cb498ee672c6344c803aab483ace79357fc4ff85a12633015488e61` |

The qualification fixture renders black while waiting and running, and dim
red only on failure, avoiding a static bright OLED workload during long soaks.
Both RetroArch processes were stopped after evidence capture and Android
reported every built-in display panel `OFF`.

The desktop soak is reproducible with a stock RetroArch executable, the built
libretro core, and the continuous CC0 fixture. Copy the core-option templates
to their disposable paths first: RetroArch rewrites a core-options file on
unload even when the main configuration is read-only in intent.

```sh
install -d /tmp/mgba-v2-qualification-save \
  /tmp/mgba-v2-qualification-state
cp tools/netpacket-spike/qualification-core-options.cfg \
  /tmp/mgba-v2-qualification-core-options.cfg

retroarch=/path/to/retroarch
core=/path/to/mgba_libretro.so
rom=/path/to/gba-link-continuous.gba
port=55448

"$retroarch" -v \
  --config tools/netpacket-spike/retroarch-qualification.cfg \
  --host --port="$port" --max-frames=180000 \
  -L "$core" "$rom" > /tmp/mgba-v2-host.log 2>&1 &
host_pid=$!
sleep 0.2
"$retroarch" -v \
  --config tools/netpacket-spike/retroarch-qualification.cfg \
  --connect=127.0.0.1 --port="$port" --max-frames=180000 \
  -L "$core" "$rom" > /tmp/mgba-v2-client.log 2>&1 &
client_pid=$!
wait "$host_pid"
wait "$client_pid"

cp tools/replicated-pair-diagnostic/core-options.cfg \
  /tmp/mgba-replicated-pair-baseline-core-options.cfg
"$retroarch" -v \
  --config tools/replicated-pair-diagnostic/retroarch-desktop.cfg \
  --max-frames=108000 -L "$core" "$rom" \
  > /tmp/mgba-v2-baseline.log 2>&1

python3 tools/analyze-replicated-netplay.py \
  /tmp/mgba-v2-host.log /tmp/mgba-v2-client.log \
  --baseline /tmp/mgba-v2-baseline.log
```

The analyzer exits nonzero for fewer than 108,000 common frames, a missing or
different sampled trace, mismatched serial counters, inadequate verification
or audio coverage, empty audio, non-frame-scaled packet counts, excessive
queue growth, or more than five-percent serial-throughput difference.

The remaining sections document protocol-v1 correctness and its earlier
physical qualification. That code remains a diagnostic SIO oracle; it is not
the normal release runtime.

## Deterministic two-core evidence

`src/gba/test/netplay-integration.c` boots the redistributable link-test ROM in
two independent `GBACore` instances. Its trace records transport generation,
timing policy, topology and effective participant counts, every sequence
domain, packet and logical sequences, cable cycle, session and transfer state,
outcome commit, deferred mode state, SIOCNT, and IF.

| Case | Evidence |
| --- | --- |
| Healthy link at all four MULTI baud rates | `twoCoresBootLinkRomAndCompleteAllBauds` |
| Ordered bounded latency and jitter | `boundedLatencyAndJitterPreserveLogicalTrace` |
| Post-START READY loss | `postStartReadyLossReplaysAtTheSameErrorCycle` |
| Healthy trace replay | Immediate and delayed runs are byte-identical |
| Error trace replay | Immediate and delayed runs are byte-identical |
| No guessed data | Error results contain only `0xFFFF` words |
| No stuck busy or missing IRQ | Both error endpoints clear busy and observe the single enabled IRQ at the same immutable completion cycle |

The test-ROM result block additionally rejects a wrong player ID, incorrect
returned word, missing/duplicate IRQ, ready/slave/error/busy mismatch, missing
two-player participation, or an incomplete baud matrix.

## Fault-injection coverage

| Protocol area | Test source and coverage |
| --- | --- |
| Packet codec | `netplay-protocol.c`: golden vectors, every message round trip, every byte truncation, trailing/oversized input, invalid enums/roles/IDs, exact `0`/`1` validation for every wire Boolean, arbitrary randomized input, and sequence exhaustion |
| Copied transport | `netplay-transport.c`: ordered reliable delivery, stale generations, inbound/outbound exhaustion, oversized packets, send failure, poll re-entry, synchronous stop, invalid callback ordering, and sequence exhaustion |
| Bilateral HELLO and acceptance | `netplay-session.c`: success, exact/conflicting duplicates, every individually dropped HELLO/ACCEPT/ACK/READY edge, ROM/profile/policy/version mismatch, third player, missing polling, and clean teardown |
| Quiescent attachment | `netplay-session.c` and `libretro-netpacket.c`: a scheduled standalone completion becoming quiescent before the admission-started deadline, permanently busy MULTI timing out without sending `HELLO`, callback/provisional-packet invalidation, both/one/neither initial MULTI snapshot, final-ack pause, synchronous start during registration, and the real RetroArch packet-before-admission callback order |
| Host-leading grants | `netplay-driver.c`: one outstanding grant, delayed ACK, withheld ACK timeout, no client overrun, no host-behind state, past-event rejection, and start truncation |
| Timing-pass pause | `netplay-driver.c`: `clientStartPauseInterruptsBeforeUncommittedCompletion` proves that reaching remote START interrupts the active timing pass and cannot consume the scheduled completion before COMMIT/catch-up |
| Mode barriers | `netplay-driver.c`: host/client intent, delayed intent, initial generation, same-cycle START ordering, pre-START precedence, missing ACK, post-START deferred host/client modes, and intent discovered at completion |
| TRANSFER_START | `netplay-driver.c`: pre-emission failure, client START acceptance, START undelivered, wrong/stale/future/conflicting sequence and cycle, explicitly tracked independent player-one start, consumption of that wait by a later remote START, and not-ready host start |
| TRANSFER_READY | `netplay-driver.c`: READY success, READY withheld, stop before READY, duplicate/conflicting READY, and immutable-cycle abort |
| TRANSFER_COMMIT | `netplay-driver.c`: success, send failure, stop after COMMIT, duplicate/conflicting COMMIT, and client remaining paused at `T` |
| TRANSFER_ABORT | `netplay-driver.c`: delayed ABORT, lost transport after accepted START, local fallback to the retained `C`, and reset/unload cancellation |
| COMPLETION_CATCHUP | `netplay-driver.c`: host-only authorisation, delayed/lost catch-up, client `T→C` execution, and timeout |
| COMPLETION_READY | `netplay-driver.c`: success, withheld/lost READY, deferred-mode and abort aggregation, and timeout |
| COMPLETION_DECISION | `netplay-driver.c`: success, send rejection, accepted-but-undelivered decision, decision-before-stop, authoritative outcome commit, and the explicitly permitted terminal role asymmetry |
| COMPLETION_DECISION_ACK | `netplay-driver.c`: healthy final release, dropped acknowledgement preserving the committed result while closing the session, and no subsequent transfer after uncertain terminal delivery |
| Detach | `netplay-driver.c`, `netplay-session.c`, and `libretro-netpacket.c`: graceful/abrupt peer loss, idle register cleanup, synchronous cleanup of a player-one wait with no completion event, active-transfer failure, frontend stop, reset, unload, and generation invalidation |
| Frontend adapter | `libretro-netpacket.c`: callback copy lifetime, reliable+flush ordering, synchronous stop during send/poll, pre-admission copied packets, wrong sender, oversize/exhaustion, all live-state save/load guards, frozen timing/cheats, reset, disconnect, and unload |

Every blocking wait is bounded by its operation-specific `GBALinkDeadline`.
Timeout cases assert the matching stable reason code. Active-transfer failure
cases assert exactly one of successful network completion, characterized error
completion, ordinary no-peer completion, or reset/unload cancellation.

## Stock RetroArch and independent workload evidence

Production qualification used unmodified RetroArch `1.22.2_GIT` (Git
`69a4f0e`) on an AYN Thor host and AYN Odin2 Portal client over the same Wi-Fi
LAN. The CC0 test ROM completed 16 transfers per endpoint across all four MULTI
baud selectors with correct words, IDs, line state, and IRQ accounting.

An independently authored workload then used afska's MIT-licensed LinkCable
`basic` example from release `v8.0.3` (ROM SHA-256
`305cbd56bf77ebe1597be5dafa5fd3617e0bf19b81b85ac56e98048133e342df`).
Both screens reported `Players: 2`, with peer IDs zero and one. A continuous
captured run completed more than 150 rapid back-to-back transactions without a
timeout, protocol failure, stuck busy bit, or disconnect. This workload exposed
and then verified the fix for timing events running beyond a newly asserted
network pause.

The production rendered run also confirms that deferring the libretro AV-info
update until after load removes the spike's second-native-window failure; both
devices used the stock OpenGL video path.

### Final clean Android qualification

The final non-instrumented ARM64 core had SHA-256
`c319602f9aebb65ebd74af7c417e6b837ff4ffdabda8235d82ce4372c5990432`.
The final CC0 test ROM had SHA-256
`24b7ef2bee7ff95ebe00d487f06ff82ea10eaefa63f04760bdb71bf9c64ffbe8`.
The host rendered the solid-yellow success result and the client rendered the
solid-blue success result after 16 transfers apiece. The host trace contained
16 START, READY, COMMIT, CATCHUP, completion-ready, decision, and final
decision-acknowledgement messages, with no link-failure diagnostic.

Measured wall-clock stalls from that clean host trace were:

| Operation | Samples | Minimum | Average | Maximum |
| --- | ---: | ---: | ---: | ---: |
| HELLO rendezvous through `SESSION_READY_ACK` | 1 | 325 ms | 325.00 ms | 325 ms |
| Initial `MODE_COMMIT` through peer `MODE_ACK` | 1 | 17 ms | 17.00 ms | 17 ms |
| `EXECUTION_GRANT` through `GRANT_ACK` | 8 | 17 ms | 23.25 ms | 28 ms |
| `TRANSFER_START` through `TRANSFER_READY` | 16 | 17 ms | 22.00 ms | 26 ms |
| `COMPLETION_CATCHUP` through `COMPLETION_READY` | 16 | 11 ms | 16.69 ms | 24 ms |
| `COMPLETION_DECISION` through final acknowledgement | 16 | 8 ms | 10.31 ms | 13 ms |
| Complete START-through-final-ack transaction | 16 | 46 ms | 49.25 ms | 55 ms |

The authoritative per-sender packet sequences ended at host sequence 81 and
client sequence 64. Because this successful run allocated one sequence for
every application send without holes, it carried 145 application packets from
first HELLO through the final acknowledgement over 1.399 seconds, or
approximately 103.6 packets per second.

The human-readable log contains 137 emitted trace lines, or 97.9 trace lines
per second, because grant trace sampling suppressed four later
`EXECUTION_GRANT` packets and four corresponding `GRANT_ACK` packets. The
transfer phase itself carried 112 transfer/barrier messages over 1.056 seconds,
or 106.1 messages per second. These are application-protocol measurements, not
raw ICMP RTT or transport overhead.

The Android NDK r27 production core also built successfully for all supported
libretro Android ABIs:

| ABI | Result |
| --- | --- |
| `arm64-v8a` | Pass, ELF AArch64, Android API 21 |
| `armeabi-v7a` | Pass, ELF ARM EABI5, Android API 21 |
| `x86` | Pass, ELF Intel 80386, Android API 21 |
| `x86_64` | Pass, ELF x86-64, Android API 21 |

No commercial ROM is stored in or required by this repository. The
independently authored LinkCable example supplies the representative
non-purpose-built Multi-Pak workload used for this qualification.

## Commands and current results

Normal focused suite:

```sh
env PYTHONPATH=/tmp/mgba-build-tools \
    LD_LIBRARY_PATH=/tmp/mgba-deps/lib64 \
  /tmp/mgba-build-tools/bin/ctest \
    --test-dir build-netplay-review \
    --output-on-failure \
    -R 'gba-netplay|gba-sio|libretro-netpacket' -j8
```

Result: 8/8 tests passed.

ASan/UBSan focused suite:

```sh
env PYTHONPATH=/tmp/mgba-build-tools \
    LD_LIBRARY_PATH=/tmp/mgba-sanitizers/usr/lib64:/tmp/mgba-deps/lib64 \
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  /tmp/mgba-build-tools/bin/ctest \
    --test-dir build-netplay-review-sanitize \
    --output-on-failure \
    -R 'gba-netplay|gba-sio|libretro-netpacket' -j4
```

Result: 8/8 tests passed with no sanitizer or leak finding.

The complete normal suite passed 28 of 29 tests. Its sole failure is the known
pinned-upstream `util-hash/stagedCrc32` case. That identical failure is recorded
in the unmodified baseline and is unrelated to this change. These results were
rerun after splitting the patch stack and rebasing it onto
`71aa6c7dab7654bfdbbd57e696f704671a97e55d`.

`.github/workflows/netplay-ci.yml` independently configures and builds these
same eight focused tests on Ubuntu 24.04 in normal and ASan/UBSan jobs. A
separate job uses the SHA-256-pinned Arm GNU Toolchain 15.2.Rel1 archive to
rebuild the CC0 ROM and compare it byte-for-byte with
`tools/gba-link-test-rom/fixtures/gba-link-test.gba`.
