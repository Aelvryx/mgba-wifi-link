#!/usr/bin/env python3
"""Deterministic smoke tests for analyze-link-netplay-log.py."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


SCRIPT = Path(__file__).with_name("analyze-link-netplay-log.py")
SPEC = importlib.util.spec_from_file_location("link_log_analyzer", SCRIPT)
assert SPEC and SPEC.loader
ANALYZER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ANALYZER
SPEC.loader.exec_module(ANALYZER)


def packet(direction: str, message: str, sequence: int, time_ms: int,
           size: int, extra: str = "") -> str:
    fields = f" {extra.strip()}" if extra.strip() else ""
    return (
        f"Status: GBA link netplay: {direction} packet type={message} "
        f"sequence={sequence}{fields} session=1 bytes={size} at_ms={time_ms}"
    )


def main() -> int:
    lines = [
        packet("send", "EXECUTION_GRANT", 1, 100, 48,
               "grant=1 horizon=280896"),
        packet("receive", "GRANT_ACK", 1, 129, 48,
               "grant=1 horizon=280896"),
        # Sender sequences 2 and 3 model grant lines hidden by trace sampling.
        packet("send", "TRANSFER_START", 4, 200, 60),
        packet("receive", "TRANSFER_READY", 4, 216, 60),
        packet("send", "TRANSFER_COMMIT", 5, 216, 64),
        packet("send", "COMPLETION_CATCHUP", 6, 216, 64),
        packet("receive", "COMPLETION_READY", 5, 232, 64),
        packet("send", "COMPLETION_DECISION", 7, 232, 68),
        packet("receive", "COMPLETION_DECISION_ACK", 6, 242, 64),
    ]
    packets = ANALYZER.parse_packets(lines)
    accounting = ANALYZER.packet_summary(packets)
    assert accounting["observed_trace_lines"] == 9
    assert accounting["inferred_application_packets"] == 13
    assert accounting["by_direction"]["send"]["inferred_suppressed_grant_lines"] == 2
    assert accounting["by_direction"]["receive"]["inferred_suppressed_grant_lines"] == 2

    grants = ANALYZER.grant_summary(packets)
    assert grants["rtt_ms"]["mean"] == 29
    assert grants["horizon_step_cycles"]["count"] == 0

    transfers = ANALYZER.transfer_summary(packets)
    assert transfers["starts"] == 1
    assert transfers["completed"] == 1
    assert transfers["start_to_ready_ms"]["mean"] == 16
    assert transfers["catchup_to_ready_ms"]["mean"] == 16
    assert transfers["decision_to_ack_ms"]["mean"] == 10
    assert transfers["start_to_final_ack_ms"]["mean"] == 42
    print("link log analyzer smoke test: pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
