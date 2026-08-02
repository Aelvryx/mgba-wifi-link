# GBA Link Fixed Delay Specification

## Purpose

Define bounded bilateral latency calibration, deterministic fixed input-delay selection, runtime wait semantics, diagnostics, and qualification gates for replicated GBA link play.

## Requirements

### Requirement: Latency calibration excludes replica work
After compatible bilateral `HELLO` and before `ACCEPT` or replica capture, the GBA Wi-Fi Link session SHALL perform a dedicated bounded latency-calibration phase. For every probe, `T0` SHALL be sampled after complete encoding and immediately before invoking the reliable flushed send callback. `T1` SHALL be sampled after the matching ACK has been received, copied, popped, decoded, and validated but before logging, formatting, the next probe, or optional work. The receiver SHALL copy, pop, decode, and validate identity/ordinal, then encode and send the ACK before diagnostics or optional work. Replica capture, serialization, compression, manifest exchange, installation, and optional logging SHALL occur outside the measured interval.

#### Scenario: Snapshot work cannot inflate RTT
- **WHEN** replica capture is artificially delayed while transport timing remains unchanged
- **THEN** the calibration samples and selected fixed input delay remain unchanged

#### Scenario: Calibration occurs before mutable replica exchange
- **WHEN** both profiles are compatible
- **THEN** the peers complete latency calibration before either authoritative replica is captured or transmitted

#### Scenario: Guest remains paused during calibration
- **WHEN** calibration spans multiple frontend callbacks
- **THEN** neither guest executes past the accepted quiescent boundary

#### Scenario: Optional receiver work follows ACK
- **WHEN** a valid probe reaches the receiver
- **THEN** its matching ACK is sent before diagnostics, formatting, capture, compression, installation, or another optional operation begins

### Requirement: Calibration has a shared provisional identity
Every pre-session `HELLO` SHALL use header session ID zero and carry a sender-selected nonzero 64-bit connection nonce that is not reused within that endpoint process lifetime. After validating both nonces and peer-equal profiles, only the host SHALL select a nonzero provisional session ID and nonzero calibration generation that are not reused within the host process lifetime and transport-generation namespace, then send `CALIBRATION_BEGIN`, echoing both nonces, probe count twelve, and calibration-policy version. Allocation SHALL fail rather than wrap or reuse a retained identity; cryptographic unpredictability is not required. The client SHALL accept `CALIBRATION_BEGIN` only for the current bilateral-HELLO state and matching nonce pair. Every calibration packet SHALL use the provisional session ID in its header and calibration generation in its payload. `ACCEPT` SHALL promote that same provisional ID to the accepted session ID.

#### Scenario: Current HELLO pair establishes calibration identity
- **WHEN** the client receives a valid host `CALIBRATION_BEGIN` that echoes both current nonces
- **THEN** both peers bind all calibration state to its provisional session ID and generation

#### Scenario: Client cannot select an independent generation
- **WHEN** bilateral HELLO validation completes
- **THEN** the client waits for the host's `CALIBRATION_BEGIN` and does not invent or reconcile a local wire identity

#### Scenario: Stale calibration packet is rejected
- **WHEN** a packet carries a nonce binding, provisional session ID, calibration generation, or transport generation from another attachment
- **THEN** it cannot contribute a sample or advance the current calibration state

#### Scenario: Identity allocation does not reuse or wrap
- **WHEN** a process allocates repeated connection nonces, provisional IDs, or calibration generations through reconnects or reaches its allocator limit
- **THEN** every retained identity remains unique and exhaustion fails the new attachment instead of wrapping

### Requirement: Calibration uses sequential bilateral bounded probe trains
Calibration SHALL execute sequentially as host probes `0…11`, host report, client probes `0…11`, client report, then host `ACCEPT`. During a probe train, only the named initiator SHALL send a probe, at most one probe SHALL be outstanding, and only the other role SHALL send its ACK. Each role SHALL originate exactly twelve probes and send its complete twelve-sample report to the other endpoint in ascending ordinal order. Both endpoints SHALL possess and validate the canonical vector `host_samples[0…11] || client_samples[0…11]` before selection or `ACCEPT`. Successful client-report send SHALL move the client to `WAIT_ACCEPT`; successful host validation of that report SHALL move the host to `CALIBRATED`; successful host `ACCEPT` send and client validation SHALL move their respective roles to `ACCEPTED`, after which existing `ACCEPT_ACK` processing continues.

#### Scenario: Clean bilateral calibration completes
- **WHEN** the host completes twelve probes/report and the client then completes twelve probes/report before the calibration deadline
- **THEN** both endpoints possess the same canonical twenty-four-sample vector and may independently select a delay

#### Scenario: Remote clocks are irrelevant
- **WHEN** endpoint wall clocks and monotonic-clock epochs differ while each monotonic source satisfies the platform duration-rate contract
- **THEN** echoed ordinals and initiator-local elapsed measurements do not require synchronized clocks

#### Scenario: Probe ordering is canonical
- **WHEN** a future, skipped, conflicting duplicate, wrong-sender, wrong-state, wrong-identity, or non-single-flight probe, acknowledgement, or report arrives
- **THEN** calibration fails closed without using that value in delay selection

#### Scenario: Semantic replay is idempotent
- **WHEN** the next valid global packet sequence repeats the immediately previous retained calibration payload while its phase permits replay
- **THEN** a duplicate probe reuses the original ACK payload without new `T0` or sample, a duplicate ACK closes no second sample, and a duplicate report neither recalculates nor replaces the vector

#### Scenario: Literal packet replay remains a sequence failure
- **WHEN** a calibration packet reuses an old global packet sequence even though its payload is identical
- **THEN** strict global sequence validation rejects it before calibration phase dispatch

#### Scenario: Calibration timeout is bounded
- **WHEN** a required probe, acknowledgement, or report does not arrive before the calibration deadline
- **THEN** the provisional session fails with a calibration-timeout reason and invalidates its transport generation

#### Scenario: Client waits separately for ACCEPT
- **WHEN** the client report send succeeds
- **THEN** client sampling completes, the client enters `WAIT_ACCEPT`, and a separate accept deadline begins without treating the report as host validation

### Requirement: Calibration wire values are exactly bounded
Probe and report durations SHALL use unsigned integer microseconds with field range `0…1,000,000` inclusive. Probe ordinals SHALL be exactly `0…11`, and each report SHALL contain exactly twelve values in ascending ordinal order. Every calibration deadline SHALL be an absolute, non-refreshing timestamp created and checked only through the fallible monotonic interface. Checked addition of 3,000,000 microseconds SHALL create `deadline_at`; addition overflow or clock-read failure SHALL fail. A governed transition SHALL be timely only when a successful read yields `now < deadline_at`; `now >= deadline_at` SHALL be expired.

The host SHALL set its calibration deadline exactly once immediately before sending `CALIBRATION_BEGIN` and retain it through host validation of the client report. The client SHALL set its calibration deadline exactly once when it validates `CALIBRATION_BEGIN` and retain it through successful sending of its report. After that send returns, one fresh successful clock read SHALL prove `now < calibration_deadline_at`; the same reading SHALL establish the client's separate absolute three-second `ACCEPT_TIMEOUT` ending when `ACCEPT` is validated. A report send returning at or after the original boundary SHALL fail as calibration timeout and SHALL NOT enter `WAIT_ACCEPT`.

After validating the client report, the host SHALL establish a separate absolute three-second `ACCEPT_ACK` deadline before committing `ACCEPTED` state and invoking the `ACCEPT` send callback. That deadline SHALL end only when the matching client `ACCEPT_ACK` is decoded and validated, after which the replica-manifest deadline takes over. No probe, ACK, report, semantic replay, duplicate report, unrelated packet, or other progress SHALL refresh any deadline. Timeout, missing data, malformed fields, conflicting duplicate, wrong identity/ordinal/state/role, out-of-range duration, queue exhaustion, transport stop, send failure, callback invalidation, clock failure, or arithmetic failure SHALL terminate the provisional session without fallback. Deadline clock failure SHALL report `CALIBRATION_CLOCK_FAILURE` during calibration and `ACCEPT_CLOCK_FAILURE` in either post-calibration accept wait.

#### Scenario: Zero-duration loopback sample is valid
- **WHEN** both reads from a conforming monotonic clock succeed with `T0 == T1` for a loopback probe
- **THEN** the zero-microsecond sample is retained canonically

#### Scenario: Duration above one second is rejected
- **WHEN** a report or measured probe duration exceeds 1,000,000 microseconds
- **THEN** calibration fails as a malformed or out-of-policy field before selection

#### Scenario: Host deadline includes both trains
- **WHEN** the host has not validated the client report within three seconds after its pre-send calibration deadline began
- **THEN** the host fails with calibration timeout even if some individual probes were valid

#### Scenario: Calibration progress cannot refresh deadline
- **WHEN** valid probes, acknowledgements, reports, or semantic replays make progress immediately before and after the original absolute calibration deadline
- **THEN** progress before the deadline is accepted, the first governed transition at or after the deadline fails, and no progress extends `deadline_at`

#### Scenario: Client sampling deadline ends at successful report send
- **WHEN** the client successfully sends its report within three seconds after validating `CALIBRATION_BEGIN`
- **THEN** its calibration deadline ends and its separate three-second `ACCEPT_TIMEOUT` begins

#### Scenario: Client report send crosses the calibration boundary
- **WHEN** the client report send callback returns at or after the original calibration deadline
- **THEN** the client fails as calibration timeout and does not enter `WAIT_ACCEPT` or establish a later accept window

#### Scenario: ACCEPT does not arrive after client report
- **WHEN** the client remains in `WAIT_ACCEPT` for three seconds without validating `ACCEPT`
- **THEN** the client fails with `ACCEPT_TIMEOUT` rather than calibration timeout

#### Scenario: Duplicate report cannot refresh ACCEPT deadline
- **WHEN** a semantic replay of the retained client report occurs while the client waits for `ACCEPT`
- **THEN** the original absolute `ACCEPT_TIMEOUT` remains unchanged

#### Scenario: ACCEPT boundary is absolute
- **WHEN** `ACCEPT` is validated once immediately before and once at or immediately after independently injected absolute deadline boundaries
- **THEN** the before-deadline case may proceed and the at-or-after case fails as `ACCEPT_TIMEOUT`

#### Scenario: Host cannot wait forever for ACCEPT_ACK
- **WHEN** the host sends `ACCEPT` successfully but no matching `ACCEPT_ACK` arrives
- **THEN** the host fails at its absolute post-`ACCEPT` deadline rather than remaining paused in `ACCEPTED`

#### Scenario: Host ACCEPT_ACK boundary is absolute and non-refreshing
- **WHEN** matching acknowledgements arrive immediately before, exactly at, and immediately after the host deadline while semantic replay or unrelated packets occur
- **THEN** only the before-deadline ACK may proceed, at-or-after fails, and no intervening traffic changes the original deadline

#### Scenario: Synchronous stop during ACCEPT is bounded
- **WHEN** the reliable `ACCEPT` send callback synchronously invalidates the transport
- **THEN** committed `ACCEPTED` state and its deadline already describe the attempted operation and teardown completes without callback reuse

#### Scenario: Deadline clock failure is distinct
- **WHEN** deadline creation or expiry checking fails even though probe timestamp reads would succeed
- **THEN** the provisional session fails with the phase-appropriate clock reason and does not substitute zero or refresh the deadline

### Requirement: Monotonic timestamp acquisition is fallible and portable
Calibration SHALL obtain timestamps through `bool monotonicTimeUs(void* context, uint64_t* timestamp)` or an equivalent interface with separate success and value. The initiator SHALL fully encode the probe, commit expected ordinal and outstanding-probe state, successfully read `T0`, and only then invoke the send callback. It SHALL successfully read `T1` after ACK validation. A failed read SHALL produce `CALIBRATION_CLOCK_FAILURE`; two successful equal readings SHALL produce a valid zero-duration sample; `T1 < T0`, subtraction overflow, or elapsed time above 1,000,000 microseconds SHALL fail calibration. GBA Wi-Fi Link builds SHALL provide conforming POSIX/Android and Windows implementations or decline to register the runtime on the unsupported platform.

#### Scenario: Clock failure differs from zero duration
- **WHEN** either timestamp acquisition returns failure
- **THEN** calibration fails as `CALIBRATION_CLOCK_FAILURE` and does not insert a zero sample

#### Scenario: Equal successful readings are valid
- **WHEN** both timestamp acquisitions succeed and `T1 == T0`
- **THEN** calibration inserts a valid zero-microsecond sample

#### Scenario: Clock moves backward
- **WHEN** both reads succeed but `T1 < T0`
- **THEN** calibration fails as a monotonic-clock failure without unsigned subtraction

#### Scenario: Send re-entry observes committed probe state
- **WHEN** the reliable send callback synchronously stops or invalidates the transport
- **THEN** the expected ordinal, outstanding-probe state, and successful `T0` already describe the attempted wire probe before failure handling runs

#### Scenario: Supported platform lacks a conforming clock
- **WHEN** protocol v2 is built on POSIX/Android, Windows, or another target without a fallible monotonic microsecond implementation
- **THEN** the runtime does not advertise or register on that target

### Requirement: Both peers authenticate the complete calibration vector
Each endpoint SHALL calculate SHA-256 over exactly: ASCII `mgba-gba-link-replicated-v2`, zero; ASCII `latency-calibration-vector-v1`, zero; little-endian `uint64 host_connection_nonce`; little-endian `uint64 client_connection_nonce`; little-endian `uint64 provisional_session_id`; little-endian `uint64 calibration_generation`; little-endian `uint32 calibration_policy_version`; little-endian `uint32 selector_policy_version`; little-endian `uint16 sample_count = 24`; little-endian `uint16 unit_identifier = 1`, where 1 means integer microseconds; then twenty-four little-endian `uint32 duration_microseconds` values ordered as host ordinals `0…11` followed by client ordinals `0…11`. `ACCEPT` and `SESSION_READY` SHALL carry the generation, vector digest, selector-policy version, calculated statistics, negotiated range, production floor, selected delay, and selection reason. The client SHALL recompute every value from its locally held complete vector.

#### Scenario: Client recomputes from all samples
- **WHEN** the client receives `ACCEPT` after both reports
- **THEN** it verifies the vector digest and independently recomputes statistics, range, floor, reason, and selected delay rather than trusting host summaries

#### Scenario: Summary with wrong vector digest fails
- **WHEN** host statistics happen to match but the canonical vector digest differs
- **THEN** the client rejects the session before replica capture

#### Scenario: Vector digest encoding is canonical
- **WHEN** both endpoints hash the same nonce pair, provisional identity, policy versions, and twenty-four samples on different supported ABIs
- **THEN** the exact digest input bytes and SHA-256 result match the frozen golden vector

### Requirement: Fixed-delay policy is deterministic and checked
Selection policy version 1 SHALL sort the twenty-four valid RTT samples in integer microseconds and use one-based nearest-rank indexing `ceil(P × 24)` to calculate `minimum_rtt = item 1`, `p50_rtt = item 12`, `p95_rtt = item 23`, and `maximum_rtt = item 24`. It SHALL calculate `base = ceil(minimum_rtt / 2)`, `variation = p95_rtt - minimum_rtt`, and `budget = base + variation + 1000 microseconds`. It SHALL then calculate `candidate = ceil((budget × 16,777,216) / (280,896 × 1,000,000))` with checked integer multiplication and quotient-plus-nonzero-remainder upward division, without first rounding the GBA frame period to integer microseconds. If `candidate` exceeds the overlapping maximum, selection SHALL fail as `CALIBRATED_TARGET_OUT_OF_RANGE`; otherwise `selected = max(candidate, 1, overlapping_minimum)`. The client SHALL recompute and verify the host's policy version, vector digest, statistics, range, floor, reason, and result before readiness.

#### Scenario: Deterministic sample set selects deterministic delay
- **WHEN** the same valid samples and supported range are supplied on any supported ABI
- **THEN** both peers calculate the same statistics, budget, candidate, and selected delay

#### Scenario: Snapshot-inflated historical value is not an input
- **WHEN** the prior ACCEPT-to-ACK duration differs from the dedicated sample statistics
- **THEN** policy version 1 uses only the dedicated calibration samples

#### Scenario: Boundary arithmetic rounds upward
- **WHEN** the exact rational numerator has a nonzero remainder after division by `280,896 × 1,000,000`
- **THEN** the candidate increases by one frame

#### Scenario: Exact frame period is not pre-rounded
- **WHEN** a selector vector lies on a boundary that would differ between 16,742- and 16,743-microsecond approximations
- **THEN** both peers use the frequency/cycles rational expression and produce the canonical result

#### Scenario: Peer cannot assert a lower result
- **WHEN** `ACCEPT` or `SESSION_READY` contains statistics or a delay that the client cannot reproduce
- **THEN** the client rejects the session before guest release

#### Scenario: Calibrated target above range fails
- **WHEN** the measured candidate exceeds the peers' overlapping maximum delay
- **THEN** attachment fails with `CALIBRATED_TARGET_OUT_OF_RANGE` rather than clamping it

#### Scenario: Candidate below minimum is raised
- **WHEN** a valid candidate is below the negotiated overlapping minimum
- **THEN** selection uses the overlapping minimum without treating the valid calibration as failure

#### Scenario: RTT half is a buffering heuristic
- **WHEN** an asymmetric path causes a packet to arrive later than the selected buffering target
- **THEN** correctness is preserved by the runtime input rendezvous and suitability is judged from wait telemetry rather than claiming the selector proves a one-way bound

### Requirement: Release floor and invalid-calibration behavior are explicit
The selector SHALL support a one-frame candidate. The default `Auto (Stable)` frontend policy SHALL advertise a two-frame minimum. The unpublished release candidate SHALL also contain `Auto (Low Latency, Experimental)`, which advertises a one-frame minimum so the exact prospective artifact can be qualified before publication. Negotiation SHALL use the stricter peer minimum, and the selected policy SHALL be frozen during every non-disconnected state. Every incomplete, malformed, timed-out, identity-invalid, transport-failed, queue-failed, or arithmetic-invalid calibration SHALL fail closed; no structurally invalid calibration SHALL select a fallback delay.

#### Scenario: Stable policy selects at least two
- **WHEN** both peers choose the default stable policy and a valid calibration calculates one frame
- **THEN** readiness commits the negotiated two-frame product minimum

#### Scenario: Qualified build may commit one frame
- **WHEN** both peers select low-latency policy and a valid calibration calculates one frame on the unpublished exact candidate
- **THEN** that candidate may commit one frame for qualification and may be published only after every one-frame acceptance criterion passes on the identical artifact

#### Scenario: Stable peer keeps a two-frame floor
- **WHEN** one peer selects stable policy and the other selects low-latency policy
- **THEN** their overlapping range has a minimum of two frames and the session cannot commit one frame

#### Scenario: Live latency-policy change is rejected
- **WHEN** a frontend variable update attempts to change stable or low-latency policy while transport is non-disconnected
- **THEN** the active range and committed delay remain unchanged

#### Scenario: Calibration failure does not become one frame
- **WHEN** the bilateral sample set is incomplete, malformed, or times out
- **THEN** the session fails rather than selecting any delay

### Requirement: Input delay is immutable for guest execution
The delay `D` committed by `SESSION_READY` SHALL remain unchanged until teardown. A physical input sampled while preparing replicated frame `F` SHALL author exactly logical frame `F + D`; every logical frame SHALL consume exactly one authoritative record from each owner. The runtime SHALL NOT predict, repeat, discard, remap, or retroactively apply inputs.

#### Scenario: Fixed mapping survives long play
- **WHEN** a session runs across input-ring wrap, periodic verification, and redundant packet delivery
- **THEN** every consumed logical frame retains the original `sample frame + D` ownership mapping exactly once

#### Scenario: Runtime delay change is requested
- **WHEN** a frontend option or peer packet attempts to alter `D` after readiness
- **THEN** the change is rejected or the session tears down before another guest frame executes

#### Scenario: Zero-frame mode is unavailable
- **WHEN** peers advertise or request zero fixed-delay frames
- **THEN** negotiation rejects the range as unsupported

### Requirement: Late input blocks without changing correctness
When an authoritative record for the current frame has not arrived, `retro_run()` SHALL use the existing bounded generation-safe receive rendezvous. It SHALL execute no replica with guessed input, return no falsely advanced frame, and fail on the input deadline if the record remains absent.

#### Scenario: Late packet arrives within deadline
- **WHEN** a valid input arrives after the frame boundary but before the input deadline
- **THEN** the call waits, consumes the authoritative record once, and advances one replicated frame with matching audio and video

#### Scenario: Packet misses deadline
- **WHEN** the required input remains absent at the deadline
- **THEN** the session fails closed and restores the latest complete local checkpoint

#### Scenario: Synchronous stop during wait
- **WHEN** the frontend synchronously invokes stop from receive polling
- **THEN** callback generation is invalidated before any stored send or poll function is reused

### Requirement: Input and verification waits are distinguished
Latency telemetry SHALL separately account for input rendezvous and periodic state-verification rendezvous. Input authoring SHALL remain after a pending verification barrier throughout this change. Reordering it SHALL require a later reviewed delta specification defining packet ordering and divergence/teardown semantics.

#### Scenario: Verification delay is not reported as network input lateness
- **WHEN** a frame waits only for a periodic state-check response
- **THEN** the duration increments verification metrics and not input-rendezvous metrics

#### Scenario: Early authoring is outside this change
- **WHEN** an implementation moves input authoring before verification completion
- **THEN** it does not conform to this change without a separately reviewed requirement

### Requirement: Latency diagnostics explain the selected policy
Attach and teardown diagnostics SHALL record the endpoint role, provisional session ID, calibration generation, vector digest, policy version, sample count, minimum, nearest-rank p50, nearest-rank p95, maximum, production floor, selected delay, and selection reason. Runtime summaries SHALL record per-player input arrival lead, input-rendezvous frame count and duration distribution, deadline misses, poll-to-send timing where available, calibration packet totals, and queue high-water marks. Physical-qualification validation SHALL require the latest attach and calibration records to match the manifest's expected endpoint role and one another's provisional session ID and calibration generation; it SHALL reject stale, mixed-session, or role-reversed records. Diagnostics SHALL exclude raw input history, ROM or save data, network addresses, and private paths.

#### Scenario: Healthy run is explainable from logs
- **WHEN** a session completes normally
- **THEN** its bounded summary identifies why its delay was selected and distinguishes fixed buffering from observed late-packet waits

#### Scenario: Sensitive data is absent
- **WHEN** latency diagnostics are emitted at attach, interval, failure, or teardown
- **THEN** they contain no button sequence, content bytes, save bytes, IP address, or private filesystem path

#### Scenario: Qualification evidence is role and session bound
- **WHEN** a runtime log reports the wrong endpoint role, disagreeing attach/calibration roles, or records from different provisional session identities
- **THEN** qualification validation fails instead of combining them into evidence for the manifest's claimed run

### Requirement: One-frame operation has an exact evidence gate
The unpublished release candidate SHALL already contain the low-latency option. That identical artifact may be published with the option only after it passes deterministic selector boundaries, byte-identical delayed and duplicate input traces, a two-device continuous run lasting at least 1,800 seconds and releasing at least 106,200 post-`SESSION_READY` frames at at least 59 FPS, zero divergence/timeouts/empty-audio frames/input loss, a human-owned Mario Kart responsiveness smoke, and a healthy stable-policy injected-jitter run.

For the normal one-frame run, the denominator SHALL be every successfully released replicated frame after `SESSION_READY`, excluding attachment/calibration and the separately impaired jitter run. A frame SHALL count as waited when it enters the input-ready receive loop at least once; multiple polls contribute to one aggregate wait duration for that frame. Verification-only waits SHALL not count. Separately on each endpoint, at least 99 percent of released frames SHALL be wait-free, nearest-rank p95 over one duration per waited frame SHALL be at most 8,000 microseconds, and maximum individual rendezvous duration SHALL be at most 16,743 microseconds, the exact GBA frame period rounded upward. If no frame waits, p95 and maximum SHALL both be zero. Failure of ratio, p95, maximum, run length/frame count, FPS, correctness, or audio on either endpoint SHALL fail the gate. The deliberately impaired stable-policy run SHALL report the same ratio/p95/maximum metrics but SHALL not apply the one-frame thresholds.

#### Scenario: All one-frame gates pass
- **WHEN** every automated and physical criterion passes on binaries tied to the reviewed source and artifact hashes
- **THEN** the release evidence may enable and document the one-frame production floor

#### Scenario: Any one-frame gate fails
- **WHEN** any required criterion fails or lacks evidence
- **THEN** that artifact is not published; the option is removed or disabled, all automated gates are rerun, and the resulting exact final stable-only artifact passes a fresh physical stable fixture and commercial smoke before release

#### Scenario: One endpoint has a tail stall
- **WHEN** either endpoint exceeds the wait-free, p95, or 16,743-microsecond maximum threshold even though the other endpoint passes
- **THEN** the one-frame gate fails for the candidate

#### Scenario: Impaired stable run reports comparable metrics
- **WHEN** deliberate jitter disqualifies one frame and stable policy is exercised
- **THEN** both endpoints still report wait-free ratio, nearest-rank p95, and maximum without applying the normal one-frame thresholds

#### Scenario: Exact-final Four Swords regression remains healthy
- **WHEN** the exact artifact selected for publication completes device qualification
- **THEN** a human-owned Four Swords run passes discovery, several verification intervals, and clean teardown without requiring another long qualification

#### Scenario: Human and automation ownership remain separate
- **WHEN** the exact Android candidate reaches commercial qualification
- **THEN** automation owns build, staging verification, supported deterministic launch, logging, analysis, teardown, and cleanup while the human owns stock RetroArch host/join, game navigation, gameplay, and subjective latency or audiovisual judgment

#### Scenario: Connection automation requires a validated interface
- **WHEN** no separately validated non-input connection interface is available on stock RetroArch
- **THEN** automation does not inject host/join hotkeys or controller events and hands connection to the human

### Requirement: Latency policy is protocol-versioned
Calibration message formats, selection policy, production-floor identity, and committed input delay SHALL be covered by the experimental protocol-v2 runtime compatibility version. Peers with an older or incompatible protocol-v2 policy SHALL fail during `HELLO`; the shipped core SHALL provide no alternate GBA link runtime and SHALL NOT automatically downgrade or attach with a mixed delay policy.

#### Scenario: Mixed latency policies reject
- **WHEN** peers advertise different required calibration or selection-policy versions
- **THEN** attachment fails before calibration or replica capture

#### Scenario: Retired protocol-v1 peer cannot attach
- **WHEN** a peer advertises or sends the retired distributed-SIO protocol v1 during attachment to a current core
- **THEN** protocol-v2 validation rejects the attachment before mutable state exchange and no fallback runtime is selected
