## Context

The replicated-pair protocol exchanges authoritative digital inputs for future emulated frames and runs both cable participants locally. This removed per-word Wi-Fi latency and is now qualified with the diagnostic workloads, Mario Kart, Advance Wars, and Four Swords. Its remaining deterministic assumptions are weaker than its execution model: protocol v2 checks exact ROM and static runtime versions, but it does not canonically compare every timing-sensitive core policy, corresponding replicas can observe different endpoint wall clocks, and physical sensor values reach only the locally owned replica.

The fixed input-delay selector also overestimates network cost. `ACCEPT` is timestamped before the client captures its replica; the client captures before sending `ACCEPT_ACK`; and the host captures its own replica before reading the acknowledgement timestamp. The recorded “RTT” therefore contains both replica captures as well as transport and callback latency. The selector then budgets half that value, adds two fixed five-millisecond jitter envelopes, rounds upward, adds another complete frame, and clamps to a minimum of two frames. The Android qualifications consequently selected four or five frames, approximately 67–84 ms, despite operating on one LAN.

Protocol v1 already contains a category-based determinism-profile builder for BIOS, CPU timing, idle optimization, RTC policy, and cheats. Protocol v2 should reuse and extend that representation rather than inventing a digest over arbitrary serialized configuration.

## Goals / Non-Goals

**Goals:**

- Reject deterministic configuration incompatibility before either peer captures or exposes replica state.
- Ensure corresponding replicas cannot observe different RTC or external-input values merely because they run on different endpoints.
- Preserve actionable category-level diagnostics without hashing paths, ABI details, or harmless presentation settings.
- Measure transport and callback timing independently from replica capture, compression, and installation.
- Select the smallest evidence-supported whole-frame fixed delay from a complete valid calibration and fail closed when calibration is incomplete or malformed.
- Permit a one-frame candidate only when deterministic tests and the exact Android pair demonstrate that its wait/stutter trade-off is acceptable.
- Preserve exact authoritative input mapping and all existing bounded failure behavior.
- Produce enough latency telemetry to distinguish intentional fixed buffering from unexpected runtime rendezvous.

**Non-Goals:**

- Prediction, rollback, run-ahead, speculative input, zero-frame remote input, or frontend rewind features.
- Changing input delay after guest execution begins.
- Synchronizing tilt, gyro, solar/luminance, camera, microphone, or other non-digital input payloads in this change.
- Four-player, Single-Pak, RFU, internet relay, reconnect, or host migration support.
- Changing local GBA SIO timing, lockstep cable semantics, pair scheduling, save ownership, or commercial-game compatibility rules.
- Forcing global RetroArch video, audio, controller, menu, or driver settings to reduce presentation latency.

## Decisions

### 1. Negotiate a canonical, category-based determinism profile

Protocol v2 will adapt the existing `GBALinkDeterminismProfileBuild()` model into a versioned protocol-v2 profile. The profile is built from explicitly enumerated values and fixed-width encodings, not a dump of `mCoreConfig`, build metadata, paths, or host structures.

The initial profile contains:

- BIOS/HLE selection and the BIOS digest when a real BIOS is active;
- emulation/timing compatibility version, timing model, overclock value, and speed-hack flags;
- idle-loop optimization policy;
- input-direction filtering policy that affects guest key state;
- RTC normalization-policy version, fake-epoch arithmetic version, and RTC semantics-model version;
- enabled-cheat state, which must be false;
- authoritative-input format version and cartridge-required external-input mask.

Profile schema version 1 begins with little-endian `uint16 schema_version = 1` and little-endian `uint16 record_count`, followed by at most sixteen strictly ascending category records. Each 36-byte record contains a little-endian 16-bit category ID, little-endian 16-bit flags, and a 32-byte SHA-256 digest. Flag bit zero means `REQUIRED`; every other flag bit is reserved and zero. The initial seven records are required. Zero records, more than sixteen records, duplicate or out-of-order categories, noncanonical Booleans, unknown enum values or bits, and nonzero reserved bytes fail decoding. Unknown required categories reject compatibility; unknown optional categories are ignored for equality and retained only for bounded diagnostics.

The initial required category order and SHA-256 domains are:

| ID | Category | Domain suffix |
| ---: | --- | --- |
| 1 | BIOS/HLE | `bios-v2` |
| 2 | CPU/timing | `cpu-timing-v2` |
| 3 | Idle optimization | `idle-optimization-v2` |
| 4 | Input policy | `input-policy-v2` |
| 5 | RTC normalization policy | `rtc-normalization-v2` |
| 6 | Cheats | `cheats-v2` |
| 7 | External authoritative-input policy | `external-input-v2` |

Every category digest is SHA-256 over the byte sequence `mgba-gba-link-replicated-v2`, a zero byte, `determinism-profile-v1`, a zero byte, the listed ASCII domain suffix, a zero byte, and the exact category payload below. All multi-byte values are little-endian; Booleans are one byte and exactly zero or one; unused/reserved bytes are zero.

| ID | Exact schema-v1 category payload |
| ---: | --- |
| 1 | `uint8 bios_mode` (`0 = HLE`, `1 = external BIOS`), `uint8 reserved[3]`, `uint8 bios_sha256[32]`. HLE requires an all-zero digest; external BIOS uses SHA-256 over the effective 16 KiB GBA BIOS image. |
| 2 | `uint32 emulation_compatibility_version`, `uint32 timing_model_flags`, `uint32 overclock_q16`, `uint32 speed_hack_mask`. Timing bit 0 means skip BIOS boot and bit 1 means request BIOS use; all other schema-v1 timing bits are zero. `0x00010000` is normal 1.0x overclock. Schema v1 defines no speed hack, so that mask is zero. |
| 3 | `uint8 idle_policy` (`0 = none/ignore`, `1 = remove`, `2 = detect`), `uint8 reserved[3]`. |
| 4 | `uint8 allow_opposing_directions`, `uint8 reserved[3]`. |
| 5 | `uint32 rtc_normalization_policy_version`, `uint32 fake_epoch_arithmetic_version`, `uint32 rtc_semantics_model_version`, `uint32 reserved`. Schema v1 requires each version to be `1`. |
| 6 | `uint8 cheats_enabled`, `uint8 reserved[3]`. The Boolean must be zero for admission. |
| 7 | `uint32 authoritative_input_format_version`, `uint32 reserved`, `uint64 cartridge_required_input_mask`. Schema v1 input format is `1`. |

The schema-v1 external-input bit assignment is `bit 0 = digital GBA keys`, `bit 1 = tilt`, `bit 2 = gyroscope`, `bit 3 = luminance/solar`, `bit 4 = camera`, and `bit 5 = microphone`; bits 6–63 are reserved. A required reserved bit rejects the session. These exact layouts let golden vectors prove a prior encoding decision instead of defining one accidentally in tests.

This structure allows the session to report `BIOS mismatch`, `timing policy mismatch`, or another specific category without logging sensitive configuration. Compiler, Android ABI, harmless build strings, visual filters, audio volume, controller mappings, paths, saves, and presentation-only settings are excluded.

Both peers exchange the peer-equal profile in `HELLO`. Local support is negotiated separately from profile equality. After the profile, every `HELLO` canonically carries little-endian `uint32 supported_rtc_source_mask`, `uint32 time_semantics_capability_mask`, `uint32 authoritative_player_rtc_source`, one-byte Boolean `content_requires_rtc`, three zero reserved bytes, and little-endian `uint64 synchronized_input_capability_mask`. RTC source bits are `bit 0 = RTC_NO_OVERRIDE`, `bit 1 = RTC_FIXED`, `bit 2 = RTC_FAKE_EPOCH`, and `bit 3 = RTC_WALLCLOCK_OFFSET`; other schema-v1 bits are reserved. Time-semantics bit 0 is `SIGNED_64BIT_TIME_T_V1`; other schema-v1 bits are reserved. The synchronized-input mask uses the external-input bit assignment above.

Exact policy-category equality and equal `content_requires_rtc` values are required, but unused capability supersets do not reject. If `content_requires_rtc` is true, both time-semantics masks must contain `SIGNED_64BIT_TIME_T_V1`. The intersection of the peers' supported RTC-source masks must contain both P0's and P1's authoritative source. The peer-equal cartridge-required input mask must be a subset of each endpoint's synchronized-input capability mask. Non-RTC content does not require signed-64-bit time semantics. Each authoritative RTC source may differ between P0 and P1, but its enum and capability must be valid. Any policy/content mismatch, unsupported source, enabled cheat, or missing required input capability terminates the provisional session before calibration, acceptance, replica capture, or mutable state exchange.

This is preferred to relying on periodic state checks because a state check discovers an incompatibility only after the differing policy has already influenced execution.

### 2. Normalize wall-clock RTC sources for the replicated pair

Corresponding replicas must not call `time(0)` independently. RTC source type and source value are player-owned state rather than peer-equal configuration: P0 may use a different supported source from P1. Peers instead require equal RTC-normalization policy, fake-epoch arithmetic, and RTC semantics-model versions; supported source masks and time semantics are negotiated local capabilities rather than equality-profile fields.

For an RTC-bearing cartridge, protocol v2 requires `SIGNED_64BIT_TIME_T_V1`: `time_t` must be a signed integer type at least 64 bits wide on both endpoints. Each `HELLO` advertises this required capability. If either endpoint lacks it, the RTC-bearing session fails before calibration or replica exchange with `RTC_TIME_SEMANTICS_MISMATCH`. Non-RTC cartridges do not require this capability and remain eligible on narrower platforms.

When an authoritative player bundle is captured from `RTC_NO_OVERRIDE` or `RTC_WALLCLOCK_OFFSET`, capture samples that player's effective Unix time once and encodes a deterministic `RTC_FAKE_EPOCH` value anchored to its emulated frame counter. Both endpoints therefore install the same epoch for that logical player, and it advances only with deterministic emulated time.

`RTC_FIXED` remains fixed. An existing `RTC_FAKE_EPOCH` remains deterministic and is transferred canonically. An unsupported custom RTC source fails before replica exchange. The P0 and P1 epochs need not equal one another: they represent separate cartridges, but both copies of P0 must agree and both copies of P1 must agree.

The conversion uses checked signed arithmetic. Let `S` be the sampled effective whole Unix seconds converted exactly to signed 64-bit, `F` the non-negative authoritative frame counter, `C` the core's cycles per frame, and `H` its frequency. Using a signed 128-bit intermediate or an equivalent checked helper:

```text
elapsed_ms = floor((F × C × 1000) / H)
fake_epoch_value_ms = (S × 1000) - elapsed_ms
```

`F × C × 1000`, `S × 1000`, the subtraction, and conversion back to the stored signed 64-bit value must all be representable. The resulting `fake_epoch_value_ms` plus `floor((UINT32_MAX × C × 1000) / H)` must also remain within signed 64-bit so every observation across the complete 32-bit GBA frame-counter domain is representable. Because both peers provide signed-at-least-64-bit `time_t`, every resulting whole-second callback value is representable on both. The sampled source exposes whole seconds, so sub-second wall-clock data is intentionally unavailable rather than silently rounded. Negative Unix dates use exact signed arithmetic. Any capability, arithmetic, or full-frame-domain representation failure rejects attachment before calibration or a bundle is sent.

The normalization is session-local and does not rewrite saved core configuration. On teardown, original source semantics take priority over exact continuity with a rolled-back checkpoint:

| Original source | Teardown result |
| --- | --- |
| `RTC_FIXED` | Restore the accepted fixed source and value exactly. |
| `RTC_FAKE_EPOCH` | Restore the accepted fake epoch, frame relationship, and value exactly. |
| `RTC_WALLCLOCK_OFFSET` | Restore the original wall-clock-offset policy and original offset; effective time may jump relative to the accepted checkpoint. |
| `RTC_NO_OVERRIDE` | Restore ordinary wall-clock policy; effective time may jump relative to the accepted checkpoint. |
| custom source | Reject before replica exchange. |

Machine state, save data, cartridge RTC metadata, and the applicable source fields are still restored transactionally. Tests cover attachment failure, verified teardown, and abrupt teardown so normalized pair state cannot partially outlive the pair. The documented wall-clock jump is external-source behavior, not partial checkpoint restoration.

This is chosen over rejecting every default wall-clock configuration because ordinary GBA RTC cartridges commonly use the default source and can be made deterministic with an existing mGBA time source. It is chosen over exchanging wall-clock samples every frame because that would create a new nondeterministic input stream.

### 3. Reject unsynchronized external-input cartridges explicitly

The current authoritative input packet contains only the ten digital GBA keys. Rotation, gyro, and luminance callbacks are endpoint-local, so permitting a cartridge that reads them would allow corresponding replicas to observe different values.

Before `HELLO`, the adapter derives the peer-equal required external-input mask from the effective cartridge hardware configuration. Each endpoint separately advertises its synchronized-input capability mask. In this change that mask contains digital keys only. Admission requires the cartridge-required mask to be a subset of both endpoint masks; an unused capability supported by only one endpoint does not reject. A cartridge requiring tilt, gyro, solar/luminance, or another unsynchronized source is rejected before replica capture with an actionable diagnostic naming the missing capability. Rumble remains local output and does not block a session.

Manual solar controls are also rejected for a solar cartridge because their resulting luminance value is not present in the authoritative input packet. A later change may extend frame inputs and compatibility negotiation; it must increment the input/runtime compatibility version.

`HW_EREADER` is a distinct unsupported cartridge-data input rather than a
schema-v1 synchronized-input bit. The adapter rejects it locally before a
compatible `HELLO`, calibration, or replica capture, including combinations
such as `HW_EREADER | HW_RUMBLE`. Current GBA cartridge discovery exposes no
equivalent camera or microphone hardware flag in this path; those inputs remain
reserved in the canonical mask and must be rejected if a future detectable
hardware source requires them.

### 4. Add a clean, bounded latency-calibration phase

Each pre-session `HELLO` uses header session ID zero and carries a nonzero 64-bit connection nonce selected independently by its sender. A connection nonce is never reused within that endpoint process lifetime. Host-selected provisional session IDs and calibration generations are likewise unique within the host process lifetime and current transport-generation namespace. Cryptographic unpredictability is not required, but allocation must fail rather than wrap to or reuse a retained value. After receiving and validating both `HELLO` packets and profiles, the host selects a nonzero provisional session ID and nonzero calibration generation and sends:

```text
CALIBRATION_BEGIN(
    provisional_session_id,
    calibration_generation,
    host_nonce,
    client_nonce,
    probe_count = 12,
    calibration_policy_version)
```

The client accepts this message only in the compatible bilateral-HELLO state and only when both echoed nonces match the current attachment. Every later calibration packet uses the provisional ID in its header and the calibration generation in its payload. `ACCEPT` promotes the same provisional ID to the accepted session ID. A stale packet from another transport generation, nonce pair, provisional ID, or calibration generation cannot enter the current sample set.

A semantic duplicate `HELLO` uses the next valid global packet sequence and repeats the immediately previous retained `HELLO` payload byte-for-byte, including nonce, profile records, optional-category set, and capability fields. Literal replay with the old packet sequence remains a sequence failure before phase dispatch. A semantic duplicate allocates no nonce, does not restart profile comparison, does not refresh any deadline, and cannot emit a second `CALIBRATION_BEGIN`. It is accepted only while that bilateral-HELLO operation remains the immediately previous retained operation; a changed payload or replay after phase advancement fails closed.

The calibration state machine is sequential:

| State | Allowed sender | Message | Required transition |
| --- | --- | --- | --- |
| `CALIBRATION_BEGIN_WAIT` | Host | `CALIBRATION_BEGIN` | Both peers enter `HOST_PROBES`; client never selects identity. |
| `HOST_PROBES` | Host | `LATENCY_PROBE(0…11)` | One host probe may be outstanding; client validates and returns the matching ACK. |
| `HOST_PROBES` | Client | `LATENCY_ACK(0…11)` | Host closes that sample before sending the next probe. |
| `HOST_REPORT` | Host | `LATENCY_REPORT(HOST, samples[12])` | Client validates/caches the complete host vector; both enter `CLIENT_PROBES`. |
| `CLIENT_PROBES` | Client | `LATENCY_PROBE(0…11)` | One client probe may be outstanding; host validates and returns the matching ACK. |
| `CLIENT_PROBES` | Host | `LATENCY_ACK(0…11)` | Client closes that sample before sending the next probe. |
| `CLIENT_REPORT` | Client | `LATENCY_REPORT(CLIENT, samples[12])` | Successful client send enters `WAIT_ACCEPT`; host validation enters `CALIBRATED`. |
| Host `CALIBRATED` / client `WAIT_ACCEPT` | Host | `ACCEPT` | Host successful send and client validation independently enter `ACCEPTED`. |
| `ACCEPTED` | Client | `ACCEPT_ACK` | Existing replica-exchange acceptance continues. |

A calibration semantic replay always uses the next valid global packet sequence while repeating the immediately previous retained calibration payload. Literal replay with an old global packet sequence remains a sequence failure before phase dispatch. A replayed probe returns the original ACK payload and creates no new `T0`, outstanding probe, or sample. A replayed ACK closes no second sample. A replayed report neither recalculates nor replaces the retained vector. Only the immediately previous retained calibration operation may be replayed. A conflicting duplicate, future ordinal, skipped ordinal, wrong sender, wrong state, wrong identity, or replay after its retention window fails closed.

Both report payloads contain the complete twelve samples in ascending ordinal order. The canonical vector is `host_samples[0…11] || client_samples[0…11]`. The vector digest is SHA-256 over exactly: ASCII `mgba-gba-link-replicated-v2`, zero; ASCII `latency-calibration-vector-v1`, zero; little-endian `uint64 host_connection_nonce`; little-endian `uint64 client_connection_nonce`; little-endian `uint64 provisional_session_id`; little-endian `uint64 calibration_generation`; little-endian `uint32 calibration_policy_version`; little-endian `uint32 selector_policy_version`; little-endian `uint16 sample_count = 24`; little-endian `uint16 unit_identifier = 1` where 1 means integer microseconds; then twenty-four little-endian `uint32 duration_microseconds` values in canonical host-then-client order. `ACCEPT` and `SESSION_READY` carry the calibration generation, vector digest, selector-policy version, calculated statistics, negotiated range, production floor, selected delay, and selection reason. The client recomputes these values from its own complete union rather than trusting host summaries.

Probe duration is unsigned integer microseconds in the inclusive range `0…1,000,000`; zero is valid when both clock reads succeed with equal values. Probe ordinals are exactly `0…11`, and reports always contain exactly twelve values.

Every calibration and accept deadline is an absolute, non-refreshing monotonic timestamp. Creating or checking one uses the fallible `monotonicTimeUs` interface; checked addition of 3,000,000 microseconds creates `deadline_at`, overflow fails, and a governed transition is timely only when a successful clock read yields `now < deadline_at`. `now >= deadline_at` is expired.

The host sets its calibration deadline exactly once immediately before sending `CALIBRATION_BEGIN`; it completes only when the client report is decoded and validated and is never refreshed by a probe, ACK, report, semantic replay, or unrelated packet. The client sets its calibration deadline exactly once when it validates `CALIBRATION_BEGIN`; it completes only when its client-report send callback returns successfully and a post-send clock read still yields `now < calibration_deadline_at`. That same successful post-send reading is used to establish the client's separate absolute three-second `ACCEPT_TIMEOUT`; the deadline ends when `ACCEPT` is decoded and validated and is never refreshed by duplicate report or unrelated traffic. A report send that returns at or after the original calibration deadline fails as calibration timeout and never enters `WAIT_ACCEPT`.

After the host validates the client report and selects the result, it creates a
separate absolute three-second `ACCEPT_ACK` deadline before committing
`ACCEPTED` state or invoking the `ACCEPT` send callback. The deadline is not
refreshed by semantic replay, unrelated packets, or other progress. It ends
only when the matching client `ACCEPT_ACK` is decoded and validated, at which
point the existing replica-manifest deadline takes over. Silence, an ACK at or
after the boundary, clock failure, or synchronous stop fails the provisional
session rather than leaving the paused host in `ACCEPTED`. Host failure to send
`ACCEPT` fails locally and eventually produces the client's accept timeout. A
failed deadline clock read produces `CALIBRATION_CLOCK_FAILURE` during either
calibration train and `ACCEPT_CLOCK_FAILURE` during either post-calibration
accept wait, then tears down the provisional session.

Timeout, missing data, malformed fields, queue exhaustion, transport stop, send failure, callback invalidation, clock failure, arithmetic failure, or out-of-bound duration terminates the provisional session. There is no invalid-calibration fallback.

The measurement boundaries are normative. The timing abstraction is fallible: `bool monotonicTimeUs(void* context, uint64_t* timestamp)`. Before invoking the reliable flushed send callback, the initiator fully encodes the probe, commits the expected ordinal and outstanding-probe state, successfully reads `T0`, and only then calls send. Synchronous callback invalidation therefore cannot put a probe on the wire without retained initiating state. `T1` is read after the matching ACK has been received, copied, popped, decoded, and validated, but before logging, formatting, sending the next probe, or optional work. On the receiver, the probe is received/copied, popped, decoded, and identity/ordinal validated; its ACK is encoded and sent before diagnostics or optional work. Allocation/copy/decoding required by the receive path is intentionally represented, while replica capture, compression, installation, and optional logging are not.

A failed `T0` or `T1` read fails as `CALIBRATION_CLOCK_FAILURE`. Two successful reads with `T1 == T0` produce a valid zero-microsecond sample. `T1 < T0`, subtraction overflow, or a duration greater than 1,000,000 microseconds fails calibration. Protocol v2 must provide a conforming fallible source for POSIX/Android and Windows; a build without one does not register protocol-v2 support.

Each elapsed measurement uses only the initiator's conforming monotonic-duration source. Different wall-clock epochs and independent monotonic epochs do not matter; every monotonic source is assumed to satisfy the platform duration-rate contract. Remote timestamps are never used.

Calibration has its own queue limits and deadline. Synchronous transport stop still invalidates callbacks before failure processing. The original cores remain paused at the accepted quiescent boundary, and no guest instruction executes because of calibration. Calibration traffic is flushed and absent after readiness. Twelve samples per role produce twenty-four values, whose nearest-rank p95 is the twenty-third value and therefore excludes one isolated maximum outlier while keeping attachment bounded.

### 5. Use a versioned fixed-delay selection policy

The selector consumes the validated canonical twenty-four-sample vector and the peers' overlapping delay range. For policy version 1:

```text
minimum_rtt = sorted_samples[0]
p50_rtt     = sorted_samples[ceil(0.50 × 24) - 1]
p95_rtt     = sorted_samples[ceil(0.95 × 24) - 1]  // item 23
maximum_rtt = sorted_samples[23]                    // item 24
base        = ceil(minimum_rtt / 2)
variation   = p95_rtt - minimum_rtt
guard       = 1000 microseconds
budget      = base + variation + guard

numerator   = budget × 16,777,216
denominator = 280,896 × 1,000,000
candidate   = ceil(numerator / denominator)

if candidate > overlapping_maximum:
    fail CALIBRATED_TARGET_OUT_OF_RANGE

selected = max(max(candidate, 1), overlapping_minimum)
```

Nearest-rank percentile indexing is one-based `ceil(P × N)` and then converted to the zero-based indexes shown above. All calculations use checked unsigned integer arithmetic. Division rounds upward as `quotient + (remainder != 0)` rather than adding `denominator - 1` unsafely. The frame-period conversion is exact rational arithmetic using the GBA ARM7TDMI frequency and video cycles per frame; it never rounds the frame period to 16,742 or 16,743 microseconds first.

The unconditional extra whole-frame guard is removed; the explicit one-millisecond scheduling guard is versioned and testable. `minimum_rtt / 2` is a buffering heuristic, not a one-way safety bound: asymmetric paths may still enter bounded runtime rendezvous. The selector targets fewer expected waits, while observed runtime input-wait telemetry and the exact-device gate determine whether one frame is suitable.

Timeout, missing data, malformed data, conflicting duplicate, identity/ordinal error, transport failure, queue failure, arithmetic failure, or incomplete reports always fail closed. A valid candidate below the negotiated product minimum is raised to that minimum. A candidate above the negotiated maximum fails as `CALIBRATED_TARGET_OUT_OF_RANGE`; it is not clamped and is not described as a proof of unsafe cable execution.

The host includes the calibration identity/digest, policy version, bounded statistics, selected delay, and selection reason in `ACCEPT` and `SESSION_READY`; the client recomputes and verifies the result from the complete vector. A peer cannot merely assert a lower delay.

The default frontend policy is `Auto (Stable)` and advertises a two-frame minimum. The unpublished release candidate also contains `Auto (Low Latency, Experimental)`, which advertises a one-frame minimum so that the exact prospective binary can be qualified. The negotiated minimum is the stricter peer's value, so one stable peer makes the session use at least two frames. The option is frozen while transport is live and its canonical policy identity is included in negotiation and logs.

If the exact candidate passes the one-frame gate, that identical artifact may be published with the option. If it fails, that artifact is not published: the option is removed or disabled in a follow-up release commit, all automated gates are rerun, and the resulting exact final stable-only artifact receives a physical stable-policy fixture and commercial smoke before publication.

### 6. Keep delay immutable and input mapping exact

Once `SESSION_READY` commits `D`, both endpoints seed and author input exactly as today: a physical sample taken for replicated frame `F` authors logical frame `F + D`, and frame `F` executes only when both authoritative records exist. No frame is skipped, repeated, guessed, or consumed twice.

Late input enters the existing bounded receive rendezvous. It may cause a short visible pause, but it cannot change the input assigned to any frame or cause replicas to diverge. The session fails on the existing input deadline rather than running ahead.

Raising or lowering `D` during play is deferred. Although possible without rollback, it requires explicit semantics for a duplicated or omitted sampling slot and would create a one-time pacing discontinuity. Connection-time calibration captures the safe latency win without that complexity.

### 7. Measure input age and wait causes before further tuning

Normal sampled diagnostics add:

- calibration identity/digest, policy version, sample count, minimum, nearest-rank p50, nearest-rank p95, maximum, selection reason, and selected delay;
- per-player input arrival lead in frames and microseconds relative to consumption;
- frames that entered input rendezvous, total and percentile wait duration, and input-deadline misses;
- separation of input waits from periodic state-verification waits;
- time from frontend input poll to successful packet send where available;
- queue high-water marks and packet/byte totals for calibration separately from runtime inputs.

Attachment and calibration records both carry the endpoint role, provisional
session ID, and calibration generation. Qualification tooling binds the latest
records to the manifest's expected role and requires their identities to match,
so stale or role-reversed records cannot be combined into one apparent run.
Compatibility failures retain the first profile category, capability mismatch
class, missing required-input mask, and local/remote schema or policy versions
for bounded actionable logging.

Metrics contain no button history, ROM data, save data, network address, or private path. Teardown emits one bounded summary. Unit tests use an injectable monotonic clock so reported percentiles and selection reasons are deterministic.

Input authoring remains after the existing periodic verification barrier. This change measures verification waits separately but does not authorize packet reordering. Moving authoring earlier would change distributed failure semantics and requires a later evidence-backed specification.

### 8. Treat one-frame operation as an evidence-gated mode

One frame is the lower bound for this non-predictive pipeline: a sample authored during `F` can be delivered while the peer completes `F` and consumed at `F + 1`. Zero frames would require the current frame to wait for a newly sampled remote input every time and would recreate a per-frame network barrier.

The unpublished candidate already contains both stable and low-latency policies. Its one-frame option may be published only when all of the following pass on that exact binary:

- deterministic fake-transport runs with latency and jitter immediately below and above the selector boundaries;
- delayed, duplicated, and deadline-edge packets preserve byte-identical input/frame traces;
- a continuous Android run lasts at least 1,800 seconds and releases at least 106,200 replicated frames while sustaining at least 59 FPS with no divergence, timeout, empty-audio frame, or input loss;
- separately on each endpoint, at least 99% of those successfully released post-`SESSION_READY` frames execute without entering the input-ready rendezvous, nearest-rank p95 over one aggregate input-wait duration per waited frame is no more than 8,000 microseconds, and no individual input rendezvous exceeds 16,743 microseconds (the exact GBA frame period rounded upward);
- a human-owned Mario Kart smoke reaches representative racing gameplay with responsive controls and clean audiovisual behavior;
- stable-policy operation on the same build remains healthy under injected jitter that intentionally disqualifies one frame and reports the same wait-free, p95, and maximum metrics without applying the one-frame thresholds.

The normal one-frame population excludes attachment, calibration, verification-only waits, and the deliberately impaired stable-policy run. A frame that enters the input-ready receive loop one or more times counts as one waited frame; all polls for that frame contribute to its one aggregate duration. If no frame waits, rendezvous p95 and maximum are reported as zero. Percentiles use the same nearest-rank rule as calibration. Failure of the wait-free ratio, p95, maximum tail bound, FPS, duration/frame minimum, or correctness/audio gate on either endpoint fails the one-frame gate.

If the gate fails, clean calibration and the two-frame default still remove the known four/five-frame overbuffering. The option is removed/disabled, creating a new final artifact; that stable-only artifact must pass the complete automated gates, a fresh exact-device stable fixture, and a brief commercial connection/gameplay smoke. No acceptance criterion requires pretending one frame is suitable when the devices say otherwise.

### 9. Version and validate the experimental protocol coherently

The new profile and calibration messages change protocol-v2 attachment and increment its runtime compatibility version. Old and new peers reject one another during `HELLO`; there is no downgrade. Packet codec validation covers exact lengths, reserved fields, ordinals, profile categories, capability masks, calibration bounds, selection policy, and canonical booleans.

The legacy distributed-SIO v1 diagnostic remains unchanged. Protocol documentation and the validation matrix record the new compatibility string/version, deterministic policy, unsupported peripherals, calibration algorithm, selected-delay evidence, and whether the production floor is one or two frames.

### 10. Preserve the established testing ownership boundary

Builds, fake-transport characterization, scripted fixtures, logs, analyzers, installation/staging verification, deterministic launch where a validated non-input interface exists, unattended soaks, evidence analysis, teardown, and cleanup are automation-owned. Stock RetroArch host/join interaction, commercial game navigation, racing, audiovisual judgment, and subjective control responsiveness are human-owned. Automation may own connection only if a separately validated interface becomes available that neither injects controller input nor alters frontend controller state. Automation stops at a prepared handoff rather than navigating an unfamiliar game or frontend by iterative screenshots and taps.

The exact final Android candidate receives a continuous-fixture qualification first. A human run is requested only after logs are armed and needs one concise action sequence. In addition to Mario Kart, the exact final artifact receives a brief Four Swords regression covering successful discovery, several verification intervals, and clean teardown; it does not repeat the earlier 27,000-frame qualification. This prevents slow input automation from becoming the test method.

## Risks / Trade-offs

- **A short calibration misses rare Wi-Fi spikes** → Use twenty-four samples whose p95 excludes one maximum outlier, retain bounded blocking, keep the stable two-frame product floor, expose arrival-lead metrics, and evidence-gate one-frame publication.
- **The proposed selector is still too conservative or too aggressive** → Version the policy and preserve raw bounded sample summaries so a later change can revise it without ambiguity.
- **Calibration adds attachment time** → Limit it to twelve sequential single-flight probes per role under a three-second deadline; it replaces contaminated measurement rather than adding an ongoing runtime exchange.
- **RTC normalization changes the time source during a session** → Use mGBA's existing deterministic fake-epoch source, preserve per-player effective time at attachment, include it in checkpoints, restore original source semantics on teardown, and document possible wall-clock jumps.
- **A narrow platform cannot reproduce canonical RTC output** → Require signed-at-least-64-bit `time_t` semantics for RTC-bearing content and reject before calibration; keep non-RTC sessions available.
- **A clock API failure resembles a zero-duration probe** → Use a fallible timestamp API, accept zero only after two successful equal reads, and fail backward/overflowed measurements explicitly on POSIX/Android and Windows.
- **Stale identities collide after a reconnect** → Allocate nonces, provisional IDs, and calibration generations without process-lifetime reuse and fail rather than wrap.
- **External-input rejection reduces apparent compatibility** → Fail before mutable state exchange with a precise message rather than allowing delayed divergence; synchronize those inputs in a later explicit capability.
- **A one-frame delay produces intermittent stalls** → Make it conditional on exact-device evidence; retain two frames as the release floor if the gate fails.
- **Configuration fingerprinting rejects harmless differences** → Hash only enumerated future-affecting categories and exclude presentation, ABI, paths, controller mappings, and build metadata.
- **Verification waiting may contribute a small periodic delay** → Measure it separately and defer any packet-ordering change to a later reviewed specification.

## Migration Plan

1. Preserve the current alpha.2 binary hashes and Android performance evidence as the four/five-frame baseline.
2. Add deterministic-profile, RTC-normalization, peripheral-capability, calibration, selection, and telemetry tests behind the incremented experimental compatibility version.
3. Run focused normal, ASan/UBSan, TSan, full normal mGBA, fixture reproducibility, and Android ARM64 build gates.
4. Build the unpublished release candidate with both stable and low-latency policies, characterize its selector under deterministic fake transport, and run the stable Android fixture.
5. If it calculates one frame, run the exact same artifact through the one-frame fixture and human-owned Mario Kart smoke while host/join remains human-owned.
6. If one-frame gates pass, publish that identical artifact. If they fail, remove/disable the option, rerun all automated gates, and run an exact-final-artifact stable fixture plus commercial smoke.
7. Update protocol, user, validation, compatibility, and release documentation with exact source and binary identity.
8. Publish the next experimental alpha only after the final fixed-delay policy and unsupported-capability behavior are documented. Rollback is reinstalling alpha.2; mixed peers cannot attach.

## Open Questions

- Does the clean calibration select one or two frames on the physical Android pair once replica capture is removed from the measurement?
- Is the one-millisecond scheduling guard sufficient for RetroArch's Android callback cadence, or does physical evidence justify a larger versioned value?
- Does verification waiting measurably delay input authoring every 60 frames enough to justify a separately specified ordering change later?
- Which current libretro/core configuration values influence emulated execution beyond the initial enumerated profile and therefore need their own stable category?
