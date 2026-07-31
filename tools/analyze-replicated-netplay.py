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


@dataclasses.dataclass
class Log:
    summaries: dict[int, Summary] = dataclasses.field(default_factory=dict)
    traces: dict[tuple[int, int], str] = dataclasses.field(default_factory=dict)
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
        print(
            f"frames={frame} checks={left.checks}/{right.checks} "
            f"packets={left.sent}/{right.sent} "
            f"serial={left.transfers}/{left.words} "
            f"audio_empty={left.empty_audio}/{right.empty_audio} "
            f"trace_samples={len(common_frames)}{baseline_text}"
        )
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
