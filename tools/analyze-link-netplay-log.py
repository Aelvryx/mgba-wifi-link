#!/usr/bin/env python3
"""Summarize mGBA Wi-Fi link traces without trusting sampled line counts."""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


PACKET_RE = re.compile(
    r"GBA link netplay: (?P<direction>send|receive) packet "
    r"type=(?P<type>[A-Z0-9_]+) sequence=(?P<sequence>\d+)"
    r"(?P<fields>.*?) session=(?P<session>\d+) bytes=(?P<bytes>\d+) "
    r"at_ms=(?P<time>\d+)"
)
FIELD_RE = re.compile(r"\b(?P<name>[a-z_]+)=(?P<value>\d+)")
PING_RE = re.compile(r"\btime[=<](?P<ms>\d+(?:\.\d+)?)\s*ms")
CPU_RE = re.compile(
    r"^\s*\d+\s+.*?\s[RSIDT]\s+(?P<cpu>\d+(?:\.\d+)?)\s+"
    r"\d+(?:\.\d+)?\s+.*\bcom\.retroarch(?:\.aarch64)?\b"
)
AUDIO_RE = re.compile(
    r"^(?P<month>\d\d)-(?P<day>\d\d) "
    r"(?P<hour>\d\d):(?P<minute>\d\d):(?P<second>\d\d)\.(?P<millis>\d\d\d)"
    r".*\bAudioTrack:\s+stop\([^)]*\): called with (?P<frames>\d+) frames delivered"
)


@dataclass(frozen=True)
class Packet:
    direction: str
    type: str
    sequence: int
    session: int
    size: int
    time_ms: int
    fields: dict[str, int]


def percentile(values: Sequence[float], percent: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = (len(ordered) - 1) * percent / 100
    low = math.floor(index)
    high = math.ceil(index)
    if low == high:
        return ordered[low]
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)


def distribution(values: Sequence[float]) -> dict[str, float | int | None]:
    if not values:
        return {"count": 0, "min": None, "median": None, "mean": None,
                "p95": None, "max": None}
    return {
        "count": len(values),
        "min": min(values),
        "median": statistics.median(values),
        "mean": statistics.fmean(values),
        "p95": percentile(values, 95),
        "max": max(values),
    }


def parse_packets(lines: Iterable[str]) -> list[Packet]:
    packets: list[Packet] = []
    for line in lines:
        match = PACKET_RE.search(line)
        if not match:
            continue
        fields = {
            field.group("name"): int(field.group("value"))
            for field in FIELD_RE.finditer(match.group("fields"))
        }
        packets.append(Packet(
            direction=match.group("direction"),
            type=match.group("type"),
            sequence=int(match.group("sequence")),
            session=int(match.group("session")),
            size=int(match.group("bytes")),
            time_ms=int(match.group("time")),
            fields=fields,
        ))
    return packets


def packet_summary(packets: Sequence[Packet]) -> dict[str, object]:
    by_direction = {
        direction: [packet for packet in packets if packet.direction == direction]
        for direction in ("send", "receive")
    }
    directions: dict[str, object] = {}
    inferred_total = 0
    inferred_bytes = 0
    for direction, selected in by_direction.items():
        sequences = {packet.sequence for packet in selected}
        maximum = max(sequences, default=0)
        observed_bytes = sum(packet.size for packet in selected)
        suppressed = max(0, maximum - len(sequences))
        # Current trace sampling suppresses only 48-byte GRANT/GRANT_ACK packets.
        directions[direction] = {
            "observed_trace_lines": len(selected),
            "observed_unique_sequences": len(sequences),
            "sequence_min": min(sequences, default=None),
            "sequence_max": maximum or None,
            "inferred_suppressed_grant_lines": suppressed,
            "observed_payload_bytes": observed_bytes,
            "inferred_payload_bytes": observed_bytes + suppressed * 48,
        }
        inferred_total += maximum
        inferred_bytes += observed_bytes + suppressed * 48
    counts: dict[str, int] = {}
    for packet in packets:
        counts[packet.type] = counts.get(packet.type, 0) + 1
    return {
        "observed_trace_lines": len(packets),
        "observed_payload_bytes": sum(packet.size for packet in packets),
        "inferred_application_packets": inferred_total,
        "inferred_application_payload_bytes": inferred_bytes,
        "by_direction": directions,
        "observed_type_counts": dict(sorted(counts.items())),
    }


def grant_summary(packets: Sequence[Packet]) -> dict[str, object]:
    sends = {
        packet.fields["grant"]: packet
        for packet in packets
        if packet.direction == "send"
        and packet.type == "EXECUTION_GRANT"
        and "grant" in packet.fields
    }
    acks = {
        packet.fields["grant"]: packet
        for packet in packets
        if packet.direction == "receive"
        and packet.type == "GRANT_ACK"
        and "grant" in packet.fields
    }
    matched = sorted(set(sends) & set(acks))
    rtt = [acks[key].time_ms - sends[key].time_ms for key in matched]
    ordered_sends = [sends[key] for key in sorted(sends)]
    intervals = [
        current.time_ms - previous.time_ms
        for previous, current in zip(ordered_sends, ordered_sends[1:])
    ]
    horizon_steps = [
        current.fields["horizon"] - previous.fields["horizon"]
        for previous, current in zip(ordered_sends, ordered_sends[1:])
        if "horizon" in previous.fields and "horizon" in current.fields
    ]
    mean_interval = statistics.fmean(intervals) if intervals else None
    return {
        "matched_sampled_grants": len(matched),
        "rtt_ms": distribution(rtt),
        "send_interval_ms": distribution(intervals),
        "horizon_step_cycles": distribution(horizon_steps),
        "inferred_emulated_fps_from_sampled_interval":
            1000 / mean_interval if mean_interval and mean_interval > 0 else None,
    }


TRANSFER_PHASES = (
    "TRANSFER_READY",
    "TRANSFER_COMMIT",
    "COMPLETION_CATCHUP",
    "COMPLETION_READY",
    "COMPLETION_DECISION",
    "COMPLETION_DECISION_ACK",
)


def transfer_summary(packets: Sequence[Packet]) -> dict[str, object]:
    starts = [
        packet for packet in packets
        if packet.direction == "send" and packet.type == "TRANSFER_START"
    ]
    records: list[dict[str, Packet]] = []
    current: dict[str, Packet] | None = None
    for packet in packets:
        if packet.direction == "send" and packet.type == "TRANSFER_START":
            if current:
                records.append(current)
            current = {"TRANSFER_START": packet}
            continue
        if current is not None and packet.type in TRANSFER_PHASES:
            expected_direction = {
                "TRANSFER_READY": "receive",
                "TRANSFER_COMMIT": "send",
                "COMPLETION_CATCHUP": "send",
                "COMPLETION_READY": "receive",
                "COMPLETION_DECISION": "send",
                "COMPLETION_DECISION_ACK": "receive",
            }[packet.type]
            if packet.direction == expected_direction and packet.type not in current:
                current[packet.type] = packet
                if packet.type == "COMPLETION_DECISION_ACK":
                    records.append(current)
                    current = None
    if current:
        records.append(current)

    completed = [
        record for record in records
        if "COMPLETION_DECISION_ACK" in record
    ]

    def elapsed(first: str, second: str) -> list[int]:
        return [
            record[second].time_ms - record[first].time_ms
            for record in completed
            if first in record and second in record
        ]

    start_intervals = [
        current.time_ms - previous.time_ms
        for previous, current in zip(starts, starts[1:])
    ]
    span = starts[-1].time_ms - starts[0].time_ms if len(starts) > 1 else 0
    return {
        "starts": len(starts),
        "completed": len(completed),
        "incomplete": len(records) - len(completed),
        "start_span_ms": span,
        "completed_words_per_second": len(completed) * 1000 / span if span else None,
        "start_interval_ms": distribution(start_intervals),
        "start_to_ready_ms": distribution(elapsed("TRANSFER_START", "TRANSFER_READY")),
        "catchup_to_ready_ms": distribution(elapsed("COMPLETION_CATCHUP", "COMPLETION_READY")),
        "decision_to_ack_ms": distribution(elapsed("COMPLETION_DECISION", "COMPLETION_DECISION_ACK")),
        "start_to_final_ack_ms": distribution(elapsed("TRANSFER_START", "COMPLETION_DECISION_ACK")),
    }


def ping_summary(path: Path | None) -> dict[str, object] | None:
    if not path:
        return None
    values = [float(match.group("ms")) for match in PING_RE.finditer(path.read_text(errors="replace"))]
    return distribution(values)


def cpu_summary(path: Path | None) -> dict[str, object] | None:
    if not path:
        return None
    values = []
    for line in path.read_text(errors="replace").splitlines():
        match = CPU_RE.match(line)
        if match:
            values.append(float(match.group("cpu")))
    return distribution(values)


def _audio_time_ms(match: re.Match[str]) -> int:
    return (((int(match.group("day")) * 24 + int(match.group("hour"))) * 60
             + int(match.group("minute"))) * 60 + int(match.group("second"))) * 1000 \
        + int(match.group("millis"))


def audio_summary(path: Path | None) -> dict[str, object] | None:
    if not path:
        return None
    events = []
    for line in path.read_text(errors="replace").splitlines():
        match = AUDIO_RE.match(line)
        if match:
            events.append((_audio_time_ms(match), int(match.group("frames"))))
    if not events:
        return {"total_stop_calls": 0}
    split = 0
    gaps = [events[index][0] - events[index - 1][0] for index in range(1, len(events))]
    if gaps and max(gaps) >= 10_000:
        split = gaps.index(max(gaps)) + 1
    cluster = events[split:]
    span = cluster[-1][0] - cluster[0][0] if len(cluster) > 1 else 0
    frames = [event[1] for event in cluster]
    return {
        "total_stop_calls": len(events),
        "storm_cluster_stop_calls": len(cluster),
        "storm_cluster_span_ms": span,
        "storm_cluster_calls_per_second": len(cluster) * 1000 / span if span else None,
        "storm_cluster_delivered_frames": distribution(frames),
        "storm_cluster_calls_at_or_below_1024_frames": sum(value <= 1024 for value in frames),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="one endpoint's RetroArch log")
    parser.add_argument("--ping", type=Path, help="optional same-run ping output")
    parser.add_argument("--top", type=Path, help="optional Android top capture")
    parser.add_argument("--logcat", type=Path, help="optional Android logcat capture")
    parser.add_argument("--pretty", action="store_true", help="indent JSON output")
    args = parser.parse_args()

    packets = parse_packets(args.log.read_text(errors="replace").splitlines())
    result = {
        "source": args.log.name,
        "packet_accounting": packet_summary(packets),
        "sampled_frame_grants": grant_summary(packets),
        "host_transfer_transactions": transfer_summary(packets),
        "ping_ms": ping_summary(args.ping),
        "retroarch_process_cpu_percent": cpu_summary(args.top),
        "android_audio_track": audio_summary(args.logcat),
    }
    print(json.dumps(result, indent=2 if args.pretty else None, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
