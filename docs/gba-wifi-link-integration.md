# GBA Wi-Fi Link product integration record

Status: active implementation record for the change that integrates the sole
replicated runtime behind one GBA Wi-Fi Link product façade. Historical v1
names in this document describe retirement constraints; they are not current
runtime surfaces.

## Immutable baseline

- Branch: `feature/integrate-gba-wifi-link`
- Exact base: `08a4db2136bfd38229aaa49cf4c603a682f115c0`
- Baseline configuration: existing Debug `build-dev`, libretro and test suite
  enabled
- Baseline focused integration result: 6/6 selected boundary tests passed
  (`gba-netplay-protocol-v2`, `gba-netplay-session-v2`,
  `gba-replicated-pair-spike`, `libretro-replicated-pair-spike`,
  `libretro-netpacket-v2`, and `libretro-netpacket-v2-replay`)
- Protected pre-change matrix: 13 focused targets plus the complete normal,
  fixture/helper, and Android ARM64 jobs

The compatibility identities frozen by this change are:

```text
protocol name:                 mgba-gba-link-replicated-v2
wire magic:                    GLR2 / 0x32524c47
protocol version:              2
runtime compatibility version: 2
host/client roles:             0 / 1
content SHA-1 length:           20 bytes
copied packet queue capacity:   64 packets
```

The protocol header, implementation, golden test, session header, and session
implementation had these SHA-256 values at the base:

```text
c9363dfc87e23fe1116df3630ff9b225c1d662910df95e45816005df11226ff4  protocol-v2.h
b075e65f3d38c3074de2429d7ae7b811e4242f1803937c7e38f9dee465681e88  protocol-v2.c
a5a167461954ff30aeb97120819ec3abaf42ad2bea6bc2133d136063e0de16a0  netplay-protocol-v2.c
e5b7326c4eda4b91c294685034d3ae8ff394d6f2c7be3327b1a33a8ac1209f82  session-v2.h
999f917c32837d43ee44ee6ab22460f9d2d5059eb4996edb5e9375d817edb66a  session-v2.c
```

The seven deterministic-profile SHA-256 category digests remain the byte
arrays asserted by `v2ProfileHasCanonicalGoldenDigests`. The calibration-vector
golden digest remains:

```text
b35e4ca8be87e71fb62be60035d481d9a6af0f896a15253b218180ce31f1c174
```

The baseline selector result for the canonical 24-sample vector remains
minimum `0 us`, p50 `11000 us`, p95 `22000 us`, maximum `23000 us`, budget
`23000 us`, selected delay `2`, and reason `CALIBRATED`.

The paired adapter replay baseline remains seven passing cases, including 125
released frames in the stable and low-latency paths, 252 normal input-batch
packets, four state-check packets, matching logical traces and cable counters,
zero input-deadline misses, matching audio accounting, checkpoint restoration,
and bounded teardown. The integration compares those semantic outputs rather
than allocator-dependent native bytes.

## Naming inventory and disposition

| Active surface at the base | Zone | Disposition |
| --- | --- | --- |
| `netpacket-v2.[ch]`, `mLibretroNetpacketV2*` | Product façade | Rename to `gba-wifi-link.[ch]`, `mLibretroGBAWifiLink*` |
| `session-v2.[ch]`, `GBALinkV2Session*`, session states/deadlines | Versioned session | Retain exactly |
| `protocol-v2.[ch]`, packet/profile/calibration types and domains | Versioned wire/schema | Retain exactly |
| `libretro-netpacket-v2` adapter and replay tests | Product façade tests | Rename to GBA Wi-Fi Link targets |
| `gba-netplay-session-v2`, `gba-netplay-protocol-v2` | Versioned implementation tests | Retain exactly |
| replicated-pair spike harnesses | Permanent scheduler/frontend coverage | Graduate to purpose names |
| `tools/netpacket-spike` configurations | Current qualification | Rename to GBA Wi-Fi Link qualification paths |
| `gba-link-runtime-selection` | Obsolete current capability | Remove after syncing positive requirements |
| `audit-protocol-v1-absence.py` | Retirement-only control | Replace with positive product/session/wire audit |
| historical v1/v2/spike reports and archived changes | Historical | Preserve names and label as historical |

Current source, CMake, CI, tool, instruction, and authoritative-spec references
are covered by the positive boundary audit. `V2`/`v2` is valid only for the
concrete session, codec, schema, digest, golden, compatibility, stale-config,
or explicitly historical zones. The old feature-branch push filters are
removed in favor of protected `master` and pull-request checks.

## Retired identities that cannot be reused

The complete retirement inventory remains authoritative in
`docs/protocol-v1-retirement.md`. In particular, no current rename may use:

```text
include/mgba/internal/gba/sio/netplay/session.h
src/gba/sio/netplay/session.c
src/platform/libretro/netpacket.h
src/platform/libretro/netpacket.c
GBALinkSession*
GBA_LINK_SESSION_*
mLibretroNetpacket*
test-gba-netplay-session
test-libretro-netpacket
gba-netplay-session
libretro-netpacket
mgba_gba_link_netplay_runtime
```

## Prototype-harness invariant ownership

| Base test case | Distinct invariant | Permanent disposition |
| --- | --- | --- |
| `cooperativePairTeardownAtCriticalBoundaries` | Cooperative local-pair teardown is safe at scheduler boundaries | Graduate with replicated-pair scheduler harness |
| `partialPairConstructionTearsDownCleanly` | Partial local-pair construction is transactional | Graduate with replicated-pair scheduler harness |
| `cooperativePairRunsContinuousLinkForTenSeconds` | Cooperative scheduler sustains continuous local cable traffic | Graduate with replicated-pair scheduler harness |
| `cooperativePairTraceIsRepeatable` | Cooperative scheduling produces repeatable logical traces | Graduate with replicated-pair scheduler harness |
| `twoWorkerPairRunsContinuousLinkForTenSeconds` | Threaded comparison remains bounded and teardown-safe | Graduate as comparative scheduler evidence |
| `runsOneFreshPairedFramePerCall` | Frontend boundary releases exactly one fresh paired frame per call | Graduate with replicated-pair frontend harness |
| `rejectsUnsupportedCoreAndIsIdempotent` | Frontend pair start/stop rejects invalid cores and is idempotent | Graduate with replicated-pair frontend harness |

The harnesses therefore remain active under permanent-purpose names; deleting
them would lose unique scheduler/threading or frontend-boundary coverage.

## Retirement-audit invariant ownership

| Old audit concern | Positive current owner |
| --- | --- |
| Sole libretro GBA Netpacket registration | GBA Wi-Fi Link façade tests and positive boundary audit |
| Stale selector absent and inert | Façade regression and positive boundary audit |
| Exact versioned protocol identity | Protocol golden tests and Android binary audit |
| Legacy bytes fail closed | Versioned session/façade regressions |
| Generation-safe callbacks and bounded queues | Transport and façade tests |
| Current names and generated targets | Positive boundary audit |
| Canonical Android product identity | Android binary audit |
| Deleted v1 files remain deleted | PR #14, retirement record, and Git history |

The positive audit has a continuing architectural purpose. It does not repeat
the full historical deletion inventory in every sanitizer job.

## Structured diagnostic contract

Human-facing status text may say GBA Wi-Fi Link and may change independently of
qualification. Diagnostic schema 1 adds these stable product-level records:

```text
product schema=1 id=mgba-gba-wifi-link protocol=mgba-gba-link-replicated-v2
failure schema=1 P<role> s=<session> generation=<generation>
        reason=<numeric-reason> state=<stable-state> frame=<replicated-frame>
```

The analyzer and Android validator consume those records rather than the
friendly registration or failure sentences. A structured failure always
rejects a run; a missing or malformed product record also fails closed. The
existing machine-readable record kinds `attach`, `calibration`, `cal-rtt`,
`cal-select`, `cal-digest-a`, `cal-digest-b`, and `determinism` retain their
current fields, role/session correlation, units, and privacy behavior. No
structured record contains paths, addresses, ROM or save data, input history,
profile digests, or BIOS data. Any incompatible record change must introduce
and validate a new diagnostic schema version.

## Final evidence

Implementation-head evidence is recorded here before independent review and is
replaced or supplemented with the immutable PR-head run at landing:

- The five frozen protocol/session files retain their exact baseline SHA-256
  values above. Protocol name, magic, compatibility versions, packet/profile
  golden vectors, and calibration digest therefore remain byte-identical.
- A normalized production diff—substituting only the approved old-to-new C
  identifiers and source include—leaves no `libretro.c` control-flow change.
  The façade implementation then differs only in human-facing messages and the
  new stable `mgba-gba-wifi-link` product identity.
- The focused normal, ASan/UBSan with leak detection, and TSan suites each pass
  all 13 current executables. The canonical façade has 12 passing cases and the
  paired real-adapter replay retains all 7 passing cases.
- The complete normal suite passes all 33 applicable executables. The separate
  pinned-upstream `util-hash/stagedCrc32` case retains its exact known failure;
  its other 17 internal cases pass.
- The analyzer smoke test, all 26 Android qualification-helper cases, and all
  8 positive-boundary policy cases pass. The source/generated-target positive
  boundary audit passes.
- A local NDK r27 ARM64 build produces an Android 21 AArch64 shared object with
  SHA-256
  `75f6e75f936d305073943a11ee2d031593b5fc3af9e1c1fc76a597da0abbc13e`.
  Its binary audit finds both `mgba-gba-wifi-link` and
  `mgba-gba-link-replicated-v2`, and no retired product identity.
- The normal host libretro core has SHA-256
  `727c5d147e4b24181162b4547be5902ae3f0ce8ee05c263947179874c88fc842`.

No calibration, selector, RTC, input, replica, save, scheduling, presentation,
audio, lifecycle, or teardown behavior changed. Commercial physical replay is
therefore intentionally not part of this naming/ownership integration gate.
