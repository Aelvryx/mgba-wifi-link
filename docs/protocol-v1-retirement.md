# Protocol-v1 retirement record

Status: implementation inventory for the removal of the retired distributed-cable
runtime. Protocol v1 is historical and is not a supported runtime after this
change. Historical packet and performance evidence remains useful as an
architectural record, but it is not a current setup path.

## Baseline

- Branch: `feature/remove-protocol-v1-runtime`
- Exact base: `a3c888308c31c4498d6218256e6e39d36b7630e9`
- Configuration: existing Debug `build-dev`, libretro and test suite enabled
- Focused result before removal: 12/12 CTest targets passed
- Paired adapter replay: 7/7 cases passed

The pre-removal focused targets were:

```text
gba-netplay-identity
gba-netplay-input-sync
gba-netplay-protocol-v2
gba-netplay-rtc-sync
gba-netplay-session-v2
gba-netplay-transport
gba-replica
gba-replicated-pair
gba-sio
libretro-netpacket-v2
libretro-netpacket-v2-replay
```

The paired replay baseline proves 125 released frames in the stable and
low-latency cases, 252 input-batch packets in the normal continuous case, four
state-check packets, equal P0/P1 frame and sampled WRAM state on both endpoints,
equal local cable transaction accounting, zero input-deadline misses, and the
seven named lifecycle/failure scenarios in
`libretro-netpacket-v2-replay.c`. The replay deliberately compares canonical
state observations rather than pinning allocator- or platform-dependent native
structure bytes to a fixed digest.

Protocol-v2 invariants frozen across the removal are:

- protocol name `mgba-gba-link-replicated-v2`;
- protocol version and runtime compatibility version `2`;
- host/client role values `0`/`1`;
- SHA-1 content identity length `20`;
- copied inbound queue capacity `64`;
- decode-status numeric values `0..13`;
- the calibration payload golden vectors in
  `calibrationPayloadsHaveGoldenLittleEndianBytes`;
- the seven SHA-256 category digests in
  `v2ProfileHasCanonicalGoldenDigests`;
- stable/low-latency selector results and exact `F -> F + D` mapping;
- transport callback-generation invalidation rules.

## Retired production and build inventory

The complete active v1 implementation is owned by these files:

```text
include/mgba/internal/gba/sio/netplay/driver.h
include/mgba/internal/gba/sio/netplay/protocol.h
include/mgba/internal/gba/sio/netplay/session.h
include/mgba/internal/gba/sio/netplay/timeline.h
src/gba/sio/netplay/driver.c
src/gba/sio/netplay/protocol.c
src/gba/sio/netplay/session.c
src/gba/sio/netplay/timeline.c
src/platform/libretro/netpacket.c
src/platform/libretro/netpacket.h
```

Their exported symbol families are `GBALinkPacket*`, `GBALinkSession*`,
`GBALinkTimeline*`, `GBALinkClock*`, `GBALinkTiming*`,
`GBASIONetplayDriver*`, and `mLibretroNetpacket*`. All v1 message, capability,
wire-mode, compatibility-policy, distributed deadline/sequence, transfer,
grant, mode-barrier, and completion-decision types in those headers retire with
the implementation.

The five retired C test targets are:

```text
gba-netplay-driver
gba-netplay-integration
gba-netplay-protocol
gba-netplay-session
libretro-netpacket
```

The current v1-only analysis surface is
`tools/analyze-link-netplay-log.py`, its self-test
`tools/test-analyze-link-netplay-log.py`, and that self-test's workflow
invocation. They are deleted rather than retained as supported archive tooling.
The qualification configuration remains useful to v2, but its stale
`mgba_gba_link_netplay_runtime` line is removed. Git history preserves all
retired sources and tools.

The frontend surface is the `mgba_gba_link_netplay_runtime` core option,
`netplayV1Diagnostic`, the option query, the v1 include, and all execution,
state, cheat, reset, unload, and configuration branches in `libretro.c`.

Current documentation references are in the root README,
`docs/wifi-link-netplay.md`, `docs/gba-link-protocol-v2.md`,
`docs/netplay-validation-matrix.md`,
`docs/commercial-netplay-performance-baseline.md`,
`docs/replicated-pair-android-feasibility.md`, and `UPSTREAM.md`. Current
instructions are updated to v2-only. Historical reports remain, labelled as
retired evidence.

## Shared dependency map

| Definition currently owned by v1 | Disposition | Frozen identity |
| --- | --- | --- |
| `GBALinkRole` | Move to neutral netplay types | host `0`, client `1` |
| `GBA_LINK_ROM_SHA1_SIZE` | Move to neutral netplay types | `20` |
| `GBA_LINK_MAX_COPIED_PACKETS` | Move to neutral netplay types | `64` |
| `GBALinkDecodeStatus` | Move to neutral netplay types | values `0..13` |
| Generic `GBALinkReason` values used by v2 transport | Move to neutral netplay types | preserve sparse values `1,2,3,9,10,11,13,22,23,24,26,27,28,29` |
| `GBALinkContentIdentity` | Keep in `identity.h` | layout and SHA-1 semantics unchanged |
| V2 profile/capability types | Keep in `identity.h` | canonical bytes/digests unchanged |
| V1 determinism profile input/digests | Retire | none |
| V1 packet/session/timeline/driver types | Retire | none |

`protocol-v2.h`, `identity.h`, and `transport.h` are changed to include the
neutral ownership directly. RTC, input synchronization, replicas, and the v2
adapter already depend only on those surviving interfaces.

## Test-case and invariant disposition

The accounting unit is each test case and each distinct behavioural invariant,
not every assertion statement.

### Retired v1 wire and distributed-timeline behaviour

All cases in `netplay-protocol.c` retire: `allMessagesRoundTrip`,
`executionGrantGoldenVector`, `truncationAndTrailingDataFailWithoutMutation`,
`oversizedAndReservedDataFail`, `invalidEnumsIdsAndRolesFail`,
`noncanonicalBooleanBytesFailWithoutMutation`,
`counterBoundaryIsRepresentableWithoutWrap`,
`conflictingDuplicateHasDistinctCanonicalBytes`,
`randomInputNeverPartiallyMutatesOutput`, and
`validPacketsWithSingleByteMutationsAreBounded`. Their invariant is the retired
v1 byte contract; the v2 codec has independent golden, mutation, length,
reserved-field, and fail-closed tests.

All cases in `netplay-session.c` retire with the distributed handshake,
host-leading execution grants, mode barriers, local/cable clock mapping, and
per-operation v1 deadline model. This includes `atomicHandshakeStopsAtFinalAckBarrier`,
`clientBecomesObservableOnlyOnPostAttachmentHostEvent`,
`runtimeYieldDefersCoalescedFollowingPacket`,
`quiescentRendezvousWaitsWithoutProcessingPeerHello`,
`alreadyUnequalModesAreCapturedWithoutLaterWrites`,
`neitherPeerInitiallyInMultiRemainsNotReady`,
`everyInterruptedHandshakePhaseTimesOutClosed`,
`exactLatestDuplicateReplaysResponseIdempotently`,
`conflictingDuplicateFailsClosed`, `romMismatchIsNamedAndRejected`,
`profileMismatchIdentifiesCategory`,
`stopDuringPollInvalidatesBeforeFurtherProcessing`,
`operationDeadlineIsSpecificAndBounded`,
`staleAndFuturePacketSequencesFailClosed`,
`activeCheatsCannotConfigureMvpSession`,
`unsupportedPolicyAndCompatibilityVersionRejectCleanly`,
`initialModeBarrierUsesBilateralAcknowledgements`,
`hostLeadingGrantIsSingleFlight`,
`rejectedModeWriteDoesNotMutateCommittedTimelineState`,
`missingGrantAcknowledgementUsesGrantDeadline`,
`deliveryLatencyDoesNotChangeModeCommitBoundary`,
`clientModeIntentCommitsAtUnpassedHostBoundary`,
`hostModeIntentFirstGrantsCatchupBoundary`, `clockMappingRejectsPastAndOverflow`,
`timingClockExtendsMtimingWrapMonotonically`, and
`timingPoliciesSeparateSchedulerGrantAndHealth`.

The v1-only grant, transfer, completion-decision, and post-START network failure
cases in `netplay-driver.c` retire. Specifically:

```text
localSchedulerQuantumDoesNotEmitPacket
grantBoundaryAcceptsBoundedTimingAccountingSlop
clientStartPauseInterruptsBeforeUncommittedCompletion
deliveryLatencyDoesNotChangeTransferOutcome
stopAfterCommitErrorCompletesAtAnnouncedCycle
startAndReadinessLossPreservePointOfNoReturn
preStartSendFailureUsesOrdinaryNoPeerTiming
commitSendFailureUsesAnnouncedCompletionCycle
completionMessageLossNeverLeavesBusySet
finalDecisionDeliveryFailureHasScopedAsymmetry
finalDecisionAckLossPreservesCommittedOutcomeAndClosesSession
explicitAbortIsReliableIdempotentAndCycleStable
resetCancellationRemovesEventWithoutIrq
unloadCancellationRemovesBothPendingEventsWithoutIrq
clientIntentBeforeStartCommitsBeforeErrorTransfer
hostModeWriteAfterStartDefersUntilErrorCompletion
clientModeWriteDuringCatchupDoesNotStrandCompletion
exactDuplicateCommitIsIdempotent
conflictingDuplicateCommitFailsClosed
futureTransferStartCannotMutateSio
```

All three `netplay-integration.c` cases retire because they exercise cable words
over the v1 wire: `twoCoresBootLinkRomAndCompleteAllBauds`,
`boundedLatencyAndJitterPreserveLogicalTrace`, and
`postStartReadyLossReplaysAtTheSameErrorCycle`. The continuous replicated-pair
and paired-adapter fixtures own the supported end-to-end workload.

### Generic SIO/local topology invariants retained elsewhere

The following `netplay-driver.c` cases prove generic behaviour that survives:

| Retired case | Distinct invariant | Surviving owner |
| --- | --- | --- |
| `quiescentAttachRejectsPendingSioCompletion` | Attachment requires quiescent SIO | `attachmentDeadlineBeginsBeforeQuiescentCapture`, v2 adapter tests |
| `hostAttachSeparatesTopologyAndEffectiveCount` | Topology and effective transfer participants differ | `noPeerMultiCharacterization`, `continuousMultiTransfersPreserveHardwareSemantics` |
| `clientAttachmentRemainsHiddenUntilHostRelease` | Pair is not guest-visible before atomic release | `bilateralBundlesInstallInCanonicalOrderAndReleaseAtomically` |
| `idleDetachCleansLinesWithoutTouchingDataOrError` | Detached line state is characterised and non-destructive | `characterizedIdleDetachState` |
| `twoDriverTransferUsesCommonCompletionAtEveryBaud` | Common SIO completion owns busy/IRQ/words | `continuousMultiTransfersPreserveHardwareSemantics`, `multiCompletionUsesLatchedMode` |
| `successfulTransferHonorsDisabledIrq` | Disabled IRQ does not fire | common `gba-sio` completion tests |
| `notReadyHostStartUsesOrdinaryNoPeerPath` | No peer uses characterised ordinary timing | `noPeerMultiCharacterization` |
| `clientIndependentStartWaitsForPrimary` | Secondary START waits for primary | `secondaryStartWaitsForPrimary` |
| `clientIndependentStartThenDisconnectRestoresIdleLines` | Orphan secondary START detaches safely | `characterizedIdleDetachState` |
| `remoteStartConsumesClientIndependentStart` | Primary START consumes secondary wait | `secondaryStartWaitsForPrimary`, continuous transfer fixture |

The generic receive-word, busy, error, completion ordering, latched-mode, and
IRQ observations are explicitly owned by `gba-sio` and
`gba-replicated-pair`; they do not depend on a network protocol.

### Lifecycle and transport invariants retained by v2

Every `libretro-netpacket.c` invariant that remains relevant is independently
owned by v2 tests:

| Retired v1 cases | Surviving owner |
| --- | --- |
| registration/admission/start cases | `registersExactReplicatedProtocol`, `clientStartsWithReliableFlushedV2Hello`, `hostAdmissionBoundsProvisionalTraffic`, `hostHelloWaitsUntilConnectedCallbackReturns` |
| quiescent/deadline/polling cases | `attachmentDeadlineBeginsBeforeQuiescentCapture`, `missingPollingAndSynchronousStopFailClosed` |
| send/poll re-entry and generation cases | `synchronousStopDuringSendInvalidatesGeneration`, `synchronousStopDuringReceivePollInvalidatesGeneration`, transport tests |
| copied queue, oversize, wrong-generation cases | `pollCopiesInboundBeforeReturn`, `inboundQueueExhaustionFailsClosed`, `oversizedPacketsFailClosed`, `oldGenerationCannotEnterNewSession` |
| live state/cheat/reset/unload/teardown guards | v2 adapter rollback tests and `detachStopResetAndUnloadReleaseBothAdapters` |
| v1 grant/frame rendezvous cases | retired; v2 owns frame readiness through authoritative input tests |

The v1-only construction/destruction smoke in `netplay-transport.c` retires;
all transport queue, reliable-send, callback-copy, re-entry, generation, and
stop/deinit cases remain in the same surviving target.

## Post-removal audit

The allowed v1 references are limited to archived OpenSpec, explicitly labelled
historical evidence, this retirement record, and one bounded raw legacy-header
vector in a v2 rejection test. Current source, generated targets, active
tooling/CI, current instructions, and the Android binary contain no supported
v1 selector, entry point, protocol name, codec, session, driver, or analyzer.

Validation on the final local patch stack produced:

- 13/13 focused tests in normal, ASan/UBSan with leak detection, and TSan;
- 33/33 applicable complete-suite tests, with the separately checked
  `util-hash/stagedCrc32` pinned-upstream exception unchanged;
- 7/7 paired-adapter replay cases, including one- and two-frame mapping,
  higher calibrated delay, RTC normalization, packet loss, attachment loss,
  teardown, reset, and unload;
- 22/22 qualification-helper tests and the replicated-log analyzer smoke;
- byte-identical checked diagnostic fixtures;
- strict OpenSpec validation;
- an Android 21 AArch64 libretro core built with NDK r27, containing the v2
  identity and no v1 compatibility string or entry point.

The active static audit is `tools/audit-protocol-v1-absence.py`. It checks the
clean source tree, generated targets, active tests and tools, current
instructions, and Android shared-object strings and dynamic symbols. Protocol
v2 wire/runtime compatibility remains version `2`; no production v2 codec,
calibration, selector, input-delay, RTC, replica, persistence, scheduling, or
teardown behaviour changed.
