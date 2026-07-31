#!/usr/bin/env python3
"""Validate paired protocol-v2 structured logs from a qualification run."""

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
    audio_samples: int = 0
    audio_frames: int = 0
    empty_audio: int = 0
    elapsed_ms: int = 0
    fps_milli: int = 0
    rv_p50_ms: int = 0
    rv_p95_ms: int = 0
    rv_max_ms: int = 0


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


@dataclasses.dataclass
class Log:
    summaries: dict[int, Summary] = dataclasses.field(default_factory=dict)
    traces: dict[tuple[int, int], str] = dataclasses.field(default_factory=dict)
    fixtures: dict[int, FixtureStatus] = dataclasses.field(default_factory=dict)
    divergence_lines: list[str] = dataclasses.field(default_factory=list)


def _values(match: re.Match[str]) -> dict[str, int]:
    return {key: int(value) for key, value in match.groupdict().items()}


def parse(path: Path) -> Log:
    result = Log()
    pending_frame: int | None = None
    pending_runtime: dict[str, int] | None = None
    for raw in path.read_text(errors="replace").splitlines():
        if "divergence frame=" in raw or "canonical state digest mismatch" in raw:
            result.divergence_lines.append(raw)
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
) -> list[str]:
    errors: list[str] = []
    if host.divergence_lines or client.divergence_lines:
        errors.append("a log contains an explicit replica divergence")
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
        fixture = (host if label == "host" else client).fixtures.get(final_frame)
        if not fixture:
            errors.append(f"{label} is missing the continuous-fixture summary")
        else:
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
    args = parser.parse_args()
    try:
        host = parse(args.host)
        client = parse(args.client)
        baseline = parse_baseline(args.baseline) if args.baseline else None
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 2
    errors = validate(host, client, args.minimum_frames, baseline)
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
            f"packet_rate={host_pps:.3f}/{client_pps:.3f}pps "
            f"byte_rate={host_bytes_per_second:.1f}/"
            f"{client_bytes_per_second:.1f}Bps "
            f"trace_samples={len(common_frames)}{baseline_text}"
        )
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
