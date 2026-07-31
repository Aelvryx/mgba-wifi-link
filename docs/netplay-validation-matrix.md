# GBA Link Netplay Validation Matrix

Date: 2026-07-30

This document maps the MVP's automated evidence to protocol phases and failure
classes. Test names below are cmocka case names inside the named source file.

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
| Packet codec | `netplay-protocol.c`: golden vectors, every message round trip, every byte truncation, trailing/oversized input, invalid enums/roles/IDs, arbitrary randomized input, and sequence exhaustion |
| Copied transport | `netplay-transport.c`: ordered reliable delivery, stale generations, inbound/outbound exhaustion, oversized packets, send failure, poll re-entry, synchronous stop, invalid callback ordering, and sequence exhaustion |
| Bilateral HELLO and acceptance | `netplay-session.c`: success, exact/conflicting duplicates, every individually dropped HELLO/ACCEPT/ACK/READY edge, ROM/profile/policy/version mismatch, third player, missing polling, and clean teardown |
| Quiescent attachment | `netplay-session.c` and `libretro-netpacket.c`: busy/pending-completion rejection, both/one/neither initial MULTI snapshot, final-ack pause, synchronous start during registration, and the real RetroArch packet-before-admission callback order |
| Host-leading grants | `netplay-driver.c`: one outstanding grant, delayed ACK, withheld ACK timeout, no client overrun, no host-behind state, past-event rejection, and start truncation |
| Timing-pass pause | `netplay-driver.c`: `clientStartPauseInterruptsBeforeUncommittedCompletion` proves that reaching remote START interrupts the active timing pass and cannot consume the scheduled completion before COMMIT/catch-up |
| Mode barriers | `netplay-driver.c`: host/client intent, delayed intent, initial generation, same-cycle START ordering, pre-START precedence, missing ACK, post-START deferred host/client modes, and intent discovered at completion |
| TRANSFER_START | `netplay-driver.c`: pre-emission failure, client START acceptance, START undelivered, wrong/stale/future/conflicting sequence and cycle, independent player-one start, and not-ready host start |
| TRANSFER_READY | `netplay-driver.c`: READY success, READY withheld, stop before READY, duplicate/conflicting READY, and immutable-cycle abort |
| TRANSFER_COMMIT | `netplay-driver.c`: success, send failure, stop after COMMIT, duplicate/conflicting COMMIT, and client remaining paused at `T` |
| TRANSFER_ABORT | `netplay-driver.c`: delayed ABORT, lost transport after accepted START, local fallback to the retained `C`, and reset/unload cancellation |
| COMPLETION_CATCHUP | `netplay-driver.c`: host-only authorisation, delayed/lost catch-up, client `T→C` execution, and timeout |
| COMPLETION_READY | `netplay-driver.c`: success, withheld/lost READY, deferred-mode and abort aggregation, and timeout |
| COMPLETION_DECISION | `netplay-driver.c`: success, send rejection, accepted-but-undelivered decision, decision-before-stop, authoritative outcome commit, and the explicitly permitted terminal role asymmetry |
| COMPLETION_DECISION_ACK | `netplay-driver.c`: healthy final release, dropped acknowledgement preserving the committed result while closing the session, and no subsequent transfer after uncertain terminal delivery |
| Detach | `netplay-driver.c`, `netplay-session.c`, and `libretro-netpacket.c`: graceful/abrupt peer loss, idle register cleanup, active-transfer failure, frontend stop, reset, unload, and generation invalidation |
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

The trace recorded 137 application messages from first HELLO through the final
acknowledgement over 1.399 seconds, or 97.9 logged messages per second. The
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
    --test-dir build-baseline-tests-pkg \
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
    --test-dir build-netplay-sanitize2 \
    --output-on-failure \
    -R 'gba-netplay|gba-sio|libretro-netpacket' -j4
```

Result: 8/8 tests passed with no sanitizer or leak finding.

The complete normal suite passed 28 of 29 tests. Its sole failure is the known
pinned-upstream `util-hash/stagedCrc32` case. That identical failure is recorded
in the unmodified baseline and is unrelated to this change.

## OpenSpec scenario audit

`openspec validate add-wifi-link-cable-netplay --strict` passes. The final
package contains 27 requirements, 127 scenarios, and 82 sequentially numbered
implementation tasks. Every requirement has at least one scenario; every
scenario has both `WHEN` and `THEN` clauses; and neither capability spec has a
duplicate scenario name.

Every session-capability scenario is covered by the codec, copied-transport,
session, or fake-libretro rows above, except rendered frontend behavior and
two-device callback/lifecycle behavior, which are covered by the recorded stock
RetroArch runs. Every Multi-Pak capability scenario is covered by the common
SIO, driver, deterministic two-core, or replay/fault-injection rows above,
except physical Wi-Fi timing and rendering, which are covered by the final
Android qualification. The unsupported-mode and disconnected serialization
scenarios additionally compare against the pinned unmodified baseline.
