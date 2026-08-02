#!/usr/bin/env python3
"""Validate paired GBA Wi-Fi Link structured logs from a qualification run."""

from __future__ import annotations

import argparse
import dataclasses
import re
import sys
from pathlib import Path


SUMMARY = re.compile(
    r"periodic P(?P<role>[01]) f=(?P<frame>\d+) "
    r"pkt=(?P<sent>\d+)/(?P<received>\d+) "
    r"B=(?P<sent_bytes>\d+)/(?P<received_bytes>\d+) "
    r"chk=(?P<checks>\d+) sio=(?P<transfers>\d+)/(?P<words>\d+)"
)
RUNTIME = re.compile(
    r"periodic P(?P<role>[01]) rv=(?P<waits>\d+)/(?P<wait_ms>\d+)ms "
    r"max=(?P<max_wait_ms>\d+) q=(?P<queue>\d+) "
    r"in=(?P<input0>\d+)/(?P<input1>\d+) "
    r"(?:lead=(?P<lead0>\d+)/(?P<lead1>\d+) )?"
    r"audio=(?P<audio_samples>\d+)/(?P<audio_frames>\d+)/"
    r"(?P<empty_audio>\d+)"
)
TRACE = re.compile(
    r"periodic P(?P<role>[01]) trace(?P<player>[01])="
    r"(?P<digest>[0-9a-f]{64})"
)
TIMING = re.compile(
    r"periodic timing P(?P<role>[01]) elapsed=(?P<elapsed_ms>\d+)ms "
    r"fps-milli=(?P<fps_milli>\d+) rv-p50=(?P<rv_p50_ms>\d+)ms "
    r"rv-p95=(?P<rv_p95_ms>\d+)ms rv-max=(?P<rv_max_ms>\d+)ms"
)
CALIBRATION_LEGACY = re.compile(
    r"calibration P(?P<role>[01]) provisional=(?P<provisional>\d+) "
    r"generation=(?P<generation>\d+) samples=(?P<samples>\d+) "
    r"min=(?P<minimum_us>\d+)us p50=(?P<p50_us>\d+)us "
    r"p95=(?P<p95_us>\d+)us max=(?P<maximum_us>\d+)us "
    r"selector=(?P<selector>\d+) floor=(?P<floor>\d+) "
    r"range=(?P<range_min>\d+)-(?P<range_max>\d+) "
    r"delay=(?P<delay>\d+) reason=(?P<reason>\d+) "
    r"digest=(?P<digest>[0-9a-f]{64})"
)
CALIBRATION_IDENTITY = re.compile(
    r"calibration P(?P<role>[01]) provisional=(?P<provisional>\d+) "
    r"generation=(?P<generation>\d+) samples=(?P<samples>\d+)"
)
CALIBRATION_RTT = re.compile(
    r"cal-rtt P(?P<role>[01]) s=(?P<session>\d+) min=(?P<minimum_us>\d+)us "
    r"p50=(?P<p50_us>\d+)us p95=(?P<p95_us>\d+)us "
    r"max=(?P<maximum_us>\d+)us"
)
CALIBRATION_SELECT = re.compile(
    r"cal-select P(?P<role>[01]) s=(?P<session>\d+) selector=(?P<selector>\d+) "
    r"floor=(?P<floor>\d+) range=(?P<range_min>\d+)-(?P<range_max>\d+) "
    r"delay=(?P<delay>\d+) reason=(?P<reason>\d+)"
)
CALIBRATION_DIGEST_A = re.compile(
    r"cal-digest-a P(?P<role>[01]) s=(?P<session>\d+) d=(?P<digest_a>[0-9a-f]{32})"
)
CALIBRATION_DIGEST_B = re.compile(
    r"cal-digest-b P(?P<role>[01]) s=(?P<session>\d+) d=(?P<digest_b>[0-9a-f]{32})"
)
INPUT_WAIT_LEGACY = re.compile(
    r"periodic input-wait P(?P<role>[01]) released=(?P<released>\d+) "
    r"waited=(?P<waited>\d+) wait-free-ppm=(?P<wait_free_ppm>\d+) "
    r"p95=(?P<input_wait_p95_us>\d+)us max=(?P<input_wait_max_us>\d+)us "
    r"total=(?P<input_wait_total_us>\d+)us "
    r"deadline-miss=(?P<input_deadline_misses>\d+) "
    r"clock-failure=(?P<telemetry_clock_failures>\d+) "
    r"poll-send=(?P<poll_send_count>\d+)/(?P<poll_send_average_us>\d+)/"
    r"(?P<poll_send_max_us>\d+)us"
)
INPUT_WAIT = re.compile(
    r"periodic input-wait P(?P<role>[01]) released=(?P<released>\d+) "
    r"waited=(?P<waited>\d+) wait-free-ppm=(?P<wait_free_ppm>\d+)"
)
INPUT_TAIL = re.compile(
    r"periodic input-tail P(?P<role>[01]) p95=(?P<input_wait_p95_us>\d+)us "
    r"max=(?P<input_wait_max_us>\d+)us total=(?P<input_wait_total_us>\d+)us"
)
INPUT_HEALTH = re.compile(
    r"periodic input-health P(?P<role>[01]) "
    r"deadline-miss=(?P<input_deadline_misses>\d+) "
    r"clock-failure=(?P<telemetry_clock_failures>\d+)"
)
POLL_SEND = re.compile(
    r"periodic poll-send P(?P<role>[01]) count=(?P<poll_send_count>\d+) "
    r"avg=(?P<poll_send_average_us>\d+)us max=(?P<poll_send_max_us>\d+)us"
)
INPUT_LEAD_LEGACY = re.compile(
    r"periodic input-lead P(?P<role>[01]) "
    r"inserts=(?P<input_insertions0>\d+)/(?P<input_insertions1>\d+) "
    r"frames-avg=(?P<input_lead_frames_average0>\d+)/"
    r"(?P<input_lead_frames_average1>\d+) "
    r"frames-max=(?P<input_lead_frames_max0>\d+)/"
    r"(?P<input_lead_frames_max1>\d+) "
    r"us-avg=(?P<input_lead_us_average0>\d+)/"
    r"(?P<input_lead_us_average1>\d+) "
    r"us-max=(?P<input_lead_us_max0>\d+)/(?P<input_lead_us_max1>\d+)"
)
INPUT_LEAD_FRAME = re.compile(
    r"periodic input-lead-frame P(?P<role>[01]) "
    r"inserts=(?P<input_insertions0>\d+)/(?P<input_insertions1>\d+) "
    r"frames-avg=(?P<input_lead_frames_average0>\d+)/"
    r"(?P<input_lead_frames_average1>\d+) "
    r"frames-max=(?P<input_lead_frames_max0>\d+)/"
    r"(?P<input_lead_frames_max1>\d+)"
)
INPUT_LEAD_TIME = re.compile(
    r"periodic input-lead-time P(?P<role>[01]) "
    r"us-avg=(?P<input_lead_us_average0>\d+)/"
    r"(?P<input_lead_us_average1>\d+) "
    r"us-max=(?P<input_lead_us_max0>\d+)/(?P<input_lead_us_max1>\d+)"
)
FIXTURE = re.compile(
    r"periodic fixture P(?P<role>[01]) "
    r"status=(?P<status0>[0-9a-fA-F]{8})/(?P<status1>[0-9a-fA-F]{8}) "
    r"transfers=(?P<transfers0>\d+)/(?P<transfers1>\d+) "
    r"errors=(?P<errors0>\d+)/(?P<errors1>\d+) "
    r"timeouts=(?P<timeouts0>\d+)/(?P<timeouts1>\d+)"
)
BASELINE = re.compile(
    r"replicated-pair diagnostic: frames=(?P<frame>\d+).*"
    r"transfers=(?P<transfers>\d+)"
)
DIAGNOSTIC_SCHEMA = 1
FAILURE = re.compile(
    r"failure schema=(?P<schema>\d+) P(?P<role>[01]) "
    r"s=(?P<session>\d+) generation=(?P<generation>\d+) "
    r"reason=(?P<reason>\d+) state=(?P<state>[a-z][a-z0-9-]*) "
    r"frame=(?P<frame>\d+)\s*$"
)


@dataclasses.dataclass(frozen=True)
class Summary:
    role: int
    frame: int
    sent: int
    received: int
    sent_bytes: int
    received_bytes: int
    checks: int
    transfers: int
    words: int
    waits: int = 0
    wait_ms: int = 0
    max_wait_ms: int = 0
    queue: int = 0
    input0: int = 0
    input1: int = 0
    lead0: int = 0
    lead1: int = 0
    audio_samples: int = 0
    audio_frames: int = 0
    empty_audio: int = 0
    elapsed_ms: int = 0
    fps_milli: int = 0
    rv_p50_ms: int = 0
    rv_p95_ms: int = 0
    rv_max_ms: int = 0
    released: int = 0
    waited: int = 0
    wait_free_ppm: int = 0
    input_wait_p95_us: int = 0
    input_wait_max_us: int = 0
    input_wait_total_us: int = 0
    input_deadline_misses: int = 0
    telemetry_clock_failures: int = 0
    poll_send_count: int = 0
    poll_send_average_us: int = 0
    poll_send_max_us: int = 0
    input_insertions0: int = 0
    input_insertions1: int = 0
    input_lead_frames_average0: int = 0
    input_lead_frames_average1: int = 0
    input_lead_frames_max0: int = 0
    input_lead_frames_max1: int = 0
    input_lead_us_average0: int = 0
    input_lead_us_average1: int = 0
    input_lead_us_max0: int = 0
    input_lead_us_max1: int = 0


@dataclasses.dataclass(frozen=True)
class Calibration:
    role: int
    provisional: int
    generation: int
    samples: int
    minimum_us: int
    p50_us: int
    p95_us: int
    maximum_us: int
    selector: int
    floor: int
    range_min: int
    range_max: int
    delay: int
    reason: int
    digest: str


@dataclasses.dataclass(frozen=True)
class FixtureStatus:
    role: int
    status0: int
    status1: int
    transfers0: int
    transfers1: int
    errors0: int
    errors1: int
    timeouts0: int
    timeouts1: int


@dataclasses.dataclass(frozen=True)
class Failure:
    schema: int
    role: int
    session: int
    generation: int
    reason: int
    state: str
    frame: int


@dataclasses.dataclass
class Log:
    summaries: dict[int, Summary] = dataclasses.field(default_factory=dict)
    traces: dict[tuple[int, int], str] = dataclasses.field(default_factory=dict)
    fixtures: dict[int, FixtureStatus] = dataclasses.field(default_factory=dict)
    divergence_lines: list[str] = dataclasses.field(default_factory=list)
    failures: list[Failure] = dataclasses.field(default_factory=list)
    calibration: Calibration | None = None


def _values(match: re.Match[str]) -> dict[str, int]:
    return {
        key: int(value)
        for key, value in match.groupdict().items()
        if value is not None
    }


def parse(path: Path) -> Log:
    result = Log()
    pending_frame: int | None = None
    pending_runtime: dict[str, int] | None = None
    calibration_parts: dict[str, int | str] = {}
    for raw in path.read_text(errors="replace").splitlines():
        if "divergence frame=" in raw or "canonical state digest mismatch" in raw:
            result.divergence_lines.append(raw)
        if "failure schema=" in raw:
            match = FAILURE.search(raw)
            if not match:
                raise ValueError(f"{path}: malformed structured failure record")
            values = match.groupdict()
            failure = Failure(
                **{
                    key: value if key == "state" else int(value)
                    for key, value in values.items()
                }
            )
            if failure.schema != DIAGNOSTIC_SCHEMA:
                raise ValueError(
                    f"{path}: unsupported failure diagnostic schema {failure.schema}"
                )
            result.failures.append(failure)
        match = CALIBRATION_LEGACY.search(raw)
        if match:
            values = match.groupdict()
            result.calibration = Calibration(
                **{
                    key: value if key == "digest" else int(value)
                    for key, value in values.items()
                }
            )
            continue
        match = CALIBRATION_IDENTITY.search(raw)
        if match:
            calibration_parts = _values(match)
            continue
        for pattern in (
            CALIBRATION_RTT,
            CALIBRATION_SELECT,
            CALIBRATION_DIGEST_A,
            CALIBRATION_DIGEST_B,
        ):
            match = pattern.search(raw)
            if not match or not calibration_parts:
                continue
            values = match.groupdict()
            if int(values["role"]) != calibration_parts.get("role"):
                raise ValueError(f"{path}: calibration role changed within one summary")
            if int(values["session"]) != calibration_parts.get("provisional"):
                raise ValueError(f"{path}: calibration session changed within one summary")
            calibration_parts.update(
                {
                    key: value if key in ("digest_a", "digest_b") else int(value)
                    for key, value in values.items()
                    if key not in ("role", "session")
                }
            )
            if "digest_a" in calibration_parts and "digest_b" in calibration_parts:
                calibration_parts["digest"] = (
                    str(calibration_parts["digest_a"])
                    + str(calibration_parts["digest_b"])
                )
            required = {field.name for field in dataclasses.fields(Calibration)}
            if required <= calibration_parts.keys():
                result.calibration = Calibration(
                    **{key: calibration_parts[key] for key in required}
                )
            break
        match = SUMMARY.search(raw)
        if match:
            values = _values(match)
            pending_frame = values["frame"]
            pending_runtime = None
            result.summaries[pending_frame] = Summary(**values)
            continue
        match = RUNTIME.search(raw)
        if match and pending_frame is not None:
            pending_runtime = _values(match)
            current = result.summaries[pending_frame]
            if pending_runtime["role"] != current.role:
                raise ValueError(f"{path}: runtime role changed at frame {pending_frame}")
            del pending_runtime["role"]
            result.summaries[pending_frame] = dataclasses.replace(
                current, **pending_runtime
            )
            continue
        match = TRACE.search(raw)
        if match and pending_frame is not None:
            values = match.groupdict()
            role = int(values["role"])
            current = result.summaries[pending_frame]
            if role != current.role:
                raise ValueError(f"{path}: trace role changed at frame {pending_frame}")
            result.traces[(pending_frame, int(values["player"]))] = values["digest"]
            continue
        match = TIMING.search(raw)
        if match and pending_frame is not None:
            values = _values(match)
            current = result.summaries[pending_frame]
            if values["role"] != current.role:
                raise ValueError(f"{path}: timing role changed at frame {pending_frame}")
            del values["role"]
            result.summaries[pending_frame] = dataclasses.replace(current, **values)
            continue
        match = INPUT_WAIT_LEGACY.search(raw)
        if match and pending_frame is not None:
            values = _values(match)
            current = result.summaries[pending_frame]
            if values["role"] != current.role:
                raise ValueError(f"{path}: input-wait role changed at frame {pending_frame}")
            del values["role"]
            result.summaries[pending_frame] = dataclasses.replace(current, **values)
            continue
        for pattern, label in (
            (INPUT_WAIT, "input-wait"),
            (INPUT_TAIL, "input-tail"),
            (INPUT_HEALTH, "input-health"),
            (POLL_SEND, "poll-send"),
        ):
            match = pattern.search(raw)
            if not match or pending_frame is None:
                continue
            values = _values(match)
            current = result.summaries[pending_frame]
            if values["role"] != current.role:
                raise ValueError(f"{path}: {label} role changed at frame {pending_frame}")
            del values["role"]
            result.summaries[pending_frame] = dataclasses.replace(current, **values)
            break
        else:
            match = None
        if match:
            continue
        match = INPUT_LEAD_LEGACY.search(raw)
        if match and pending_frame is not None:
            values = _values(match)
            current = result.summaries[pending_frame]
            if values["role"] != current.role:
                raise ValueError(f"{path}: input-lead role changed at frame {pending_frame}")
            del values["role"]
            result.summaries[pending_frame] = dataclasses.replace(current, **values)
            continue
        for pattern, label in (
            (INPUT_LEAD_FRAME, "input-lead-frame"),
            (INPUT_LEAD_TIME, "input-lead-time"),
        ):
            match = pattern.search(raw)
            if not match or pending_frame is None:
                continue
            values = _values(match)
            current = result.summaries[pending_frame]
            if values["role"] != current.role:
                raise ValueError(f"{path}: {label} role changed at frame {pending_frame}")
            del values["role"]
            result.summaries[pending_frame] = dataclasses.replace(current, **values)
            break
        if match:
            continue
        match = FIXTURE.search(raw)
        if match and pending_frame is not None:
            values = match.groupdict()
            fixture = FixtureStatus(
                role=int(values["role"]),
                status0=int(values["status0"], 16),
                status1=int(values["status1"], 16),
                transfers0=int(values["transfers0"]),
                transfers1=int(values["transfers1"]),
                errors0=int(values["errors0"]),
                errors1=int(values["errors1"]),
                timeouts0=int(values["timeouts0"]),
                timeouts1=int(values["timeouts1"]),
            )
            if fixture.role != result.summaries[pending_frame].role:
                raise ValueError(f"{path}: fixture role changed at frame {pending_frame}")
            result.fixtures[pending_frame] = fixture
    return result


def parse_baseline(path: Path) -> tuple[int, int]:
    latest: tuple[int, int] | None = None
    for raw in path.read_text(errors="replace").splitlines():
        match = BASELINE.search(raw)
        if match:
            latest = (int(match["frame"]), int(match["transfers"]))
    if latest is None:
        raise ValueError(f"{path}: no replicated-pair baseline summary")
    return latest


def validate(
    host: Log,
    client: Log,
    minimum_frames: int,
    baseline: tuple[int, int] | None,
    require_fixture: bool = True,
    expected_policy: int | None = None,
    expected_delay: int | None = None,
    one_frame_gate: bool = False,
) -> list[str]:
    errors: list[str] = []
    if host.divergence_lines or client.divergence_lines:
        errors.append("a log contains an explicit replica divergence")
    if host.failures or client.failures:
        errors.append("a log contains an explicit replicated-link failure")
    for label, log in (("host", host), ("client", client)):
        expected_role = 0 if label == "host" else 1
        if log.calibration:
            expected_role = log.calibration.role
        for failure in log.failures:
            if failure.role != expected_role:
                errors.append(
                    f"{label} failure role P{failure.role} does not match P{expected_role}"
                )
            if log.calibration and failure.session not in (
                0,
                log.calibration.provisional,
            ):
                errors.append(f"{label} failure belongs to a different session")
            if log.calibration and failure.generation not in (
                0,
                log.calibration.generation,
            ):
                errors.append(f"{label} failure belongs to a different generation")
    if expected_policy is not None or expected_delay is not None or one_frame_gate:
        if not host.calibration or not client.calibration:
            errors.append("calibration summary is missing on one or both endpoints")
        elif host.calibration != dataclasses.replace(
            client.calibration, role=host.calibration.role
        ):
            errors.append("calibration summaries differ between endpoints")
        else:
            if host.calibration.samples != 24:
                errors.append("calibration does not contain 24 samples")
            if expected_policy is not None and host.calibration.floor != expected_policy:
                errors.append(
                    f"calibration floor is {host.calibration.floor}; "
                    f"expected {expected_policy}"
                )
            if expected_delay is not None and host.calibration.delay != expected_delay:
                errors.append(
                    f"selected delay is {host.calibration.delay}; "
                    f"expected {expected_delay}"
                )
    host_only = sorted(host.summaries.keys() - client.summaries.keys())
    client_only = sorted(client.summaries.keys() - host.summaries.keys())
    if host_only:
        errors.append(f"host-only periodic summaries: {host_only}")
    if client_only:
        errors.append(f"client-only periodic summaries: {client_only}")
    common_frames = sorted(host.summaries.keys() & client.summaries.keys())
    if not common_frames:
        return errors + ["no common periodic summaries"]
    final_frame = common_frames[-1]
    if final_frame < minimum_frames:
        errors.append(f"only {final_frame} replicated frames; need {minimum_frames}")
    previous_leads = {"host": (0, 0), "client": (0, 0)}
    for frame in common_frames:
        for player in range(2):
            key = (frame, player)
            if key not in host.traces or key not in client.traces:
                errors.append(f"missing rolling trace P{player} at frame {frame}")
            elif host.traces[key] != client.traces[key]:
                errors.append(f"rolling trace P{player} differs at frame {frame}")
        left = host.summaries[frame]
        right = client.summaries[frame]
        if (left.transfers, left.words) != (right.transfers, right.words):
            errors.append(f"serial counters differ at frame {frame}")
        if (left.lead0, left.lead1) != (right.lead0, right.lead1):
            errors.append(f"recovered frame-lead counters differ at frame {frame}")
        for label, summary in (("host", left), ("client", right)):
            leads = (summary.lead0, summary.lead1)
            if any(
                current < previous
                for current, previous in zip(leads, previous_leads[label])
            ):
                errors.append(
                    f"{label} recovered frame-lead counter decreased at frame {frame}"
                )
            previous_leads[label] = leads
    for label, summary in (
        ("host", host.summaries[final_frame]),
        ("client", client.summaries[final_frame]),
    ):
        minimum_checks = final_frame // 60 - 1
        if summary.checks < minimum_checks:
            errors.append(
                f"{label} has {summary.checks} verified checks; need {minimum_checks}"
            )
        if summary.empty_audio:
            errors.append(f"{label} reported {summary.empty_audio} empty audio frames")
        if summary.audio_frames < final_frame - 2:
            errors.append(
                f"{label} reported audio for {summary.audio_frames}/{final_frame} frames"
            )
        if summary.sent > final_frame * 2:
            errors.append(
                f"{label} packet rate is not frame-scaled: {summary.sent}/{final_frame}"
            )
        if summary.queue > 64:
            errors.append(f"{label} copied queue high-water is {summary.queue}")
        if summary.fps_milli < 59_000:
            errors.append(
                f"{label} ran at {summary.fps_milli / 1000:.3f} FPS; need 59 FPS"
            )
        if summary.waited > summary.released:
            errors.append(f"{label} input waited-frame count exceeds released frames")
        expected_wait_free = (
            (summary.released - summary.waited) * 1_000_000
            // summary.released
            if summary.released and summary.waited <= summary.released
            else 0
        )
        if summary.wait_free_ppm != expected_wait_free:
            errors.append(f"{label} input wait-free ratio is internally inconsistent")
        if summary.input_wait_p95_us > summary.input_wait_max_us:
            errors.append(f"{label} input-wait percentiles are malformed")
        if not summary.waited and (
            summary.input_wait_p95_us
            or summary.input_wait_max_us
            or summary.input_wait_total_us
        ):
            errors.append(f"{label} zero-wait run reports a nonzero input-wait duration")
        if summary.input_deadline_misses:
            errors.append(f"{label} reported an input deadline miss")
        if summary.telemetry_clock_failures:
            errors.append(f"{label} reported a telemetry clock failure")
        if one_frame_gate:
            if summary.released < 106_200:
                errors.append(
                    f"{label} released {summary.released} frames; need 106200"
                )
            if summary.elapsed_ms < 1_800_000:
                errors.append(
                    f"{label} ran for {summary.elapsed_ms}ms; need 1800000ms"
                )
            if summary.wait_free_ppm < 990_000:
                errors.append(
                    f"{label} wait-free ratio is {summary.wait_free_ppm}ppm"
                )
            if summary.input_wait_p95_us > 8_000:
                errors.append(
                    f"{label} input-wait p95 is {summary.input_wait_p95_us}us"
                )
            if summary.input_wait_max_us > 16_743:
                errors.append(
                    f"{label} input-wait maximum is {summary.input_wait_max_us}us"
                )
        fixture = (host if label == "host" else client).fixtures.get(final_frame)
        if require_fixture and not fixture:
            errors.append(f"{label} is missing the continuous-fixture summary")
        elif fixture:
            if (fixture.status0, fixture.status1) != (3, 3):
                errors.append(
                    f"{label} fixture status is "
                    f"{fixture.status0:#010x}/{fixture.status1:#010x}"
                )
            if fixture.transfers0 != fixture.transfers1:
                errors.append(f"{label} fixture transfer counters differ")
            if fixture.errors0 or fixture.errors1:
                errors.append(f"{label} fixture reported data/line/IRQ errors")
            if fixture.timeouts0 or fixture.timeouts1:
                errors.append(f"{label} fixture reported a transfer timeout")
    if not require_fixture:
        final = host.summaries[final_frame]
        if not final.transfers:
            errors.append("commercial run completed no MULTI transfers")
        elif final.words != final.transfers * 2:
            errors.append(
                "commercial run serial word count does not match two-player MULTI"
            )
    if baseline:
        baseline_frame, baseline_transfers = baseline
        if baseline_frame <= 0:
            return errors + ["local baseline contains no completed frames"]
        baseline_rate = baseline_transfers / baseline_frame
        network_rate = host.summaries[final_frame].transfers / final_frame
        difference = abs(network_rate / baseline_rate - 1)
        if difference > 0.05:
            errors.append(
                "serial throughput differs from local baseline by "
                f"{difference * 100:.3f}%"
            )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host", type=Path)
    parser.add_argument("client", type=Path)
    parser.add_argument("--minimum-frames", type=int, default=108_000)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument(
        "--commercial",
        action="store_true",
        help="validate a real-game run without the CC0 fixture result block",
    )
    parser.add_argument("--expected-floor", type=int, choices=(1, 2))
    parser.add_argument("--expected-delay", type=int)
    parser.add_argument("--one-frame-gate", action="store_true")
    args = parser.parse_args()
    try:
        host = parse(args.host)
        client = parse(args.client)
        baseline = parse_baseline(args.baseline) if args.baseline else None
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 2
    errors = validate(
        host,
        client,
        args.minimum_frames,
        baseline,
        require_fixture=not args.commercial,
        expected_policy=args.expected_floor,
        expected_delay=args.expected_delay,
        one_frame_gate=args.one_frame_gate,
    )
    common_frames = sorted(host.summaries.keys() & client.summaries.keys())
    if common_frames:
        frame = common_frames[-1]
        left = host.summaries[frame]
        right = client.summaries[frame]
        baseline_text = ""
        if baseline:
            baseline_rate = baseline[1] / baseline[0]
            network_rate = left.transfers / frame
            baseline_text = (
                f" baseline_delta={abs(network_rate / baseline_rate - 1) * 100:.3f}%"
            )
        host_pps = left.sent * 1000 / left.elapsed_ms if left.elapsed_ms else 0
        client_pps = right.sent * 1000 / right.elapsed_ms if right.elapsed_ms else 0
        host_bytes_per_second = (
            left.sent_bytes * 1000 / left.elapsed_ms if left.elapsed_ms else 0
        )
        client_bytes_per_second = (
            right.sent_bytes * 1000 / right.elapsed_ms if right.elapsed_ms else 0
        )
        print(
            f"frames={frame} checks={left.checks}/{right.checks} "
            f"packets={left.sent}/{right.sent} "
            f"serial={left.transfers}/{left.words} "
            f"audio_empty={left.empty_audio}/{right.empty_audio} "
            f"fps={left.fps_milli / 1000:.3f}/{right.fps_milli / 1000:.3f} "
            f"rv_p50={left.rv_p50_ms}/{right.rv_p50_ms}ms "
            f"rv_p95={left.rv_p95_ms}/{right.rv_p95_ms}ms "
            f"rv_max={left.rv_max_ms}/{right.rv_max_ms}ms "
            f"input_wait={left.waited}/{right.waited} "
            f"input_p95={left.input_wait_p95_us}/"
            f"{right.input_wait_p95_us}us "
            f"input_max={left.input_wait_max_us}/"
            f"{right.input_wait_max_us}us "
            f"packet_rate={host_pps:.3f}/{client_pps:.3f}pps "
            f"byte_rate={host_bytes_per_second:.1f}/"
            f"{client_bytes_per_second:.1f}Bps "
            f"lead={left.lead0}/{left.lead1} "
            f"trace_samples={len(common_frames)}{baseline_text}"
        )
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
