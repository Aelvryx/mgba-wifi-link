#!/usr/bin/env python3
"""Deterministic smoke tests for analyze-gba-wifi-link.py."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("analyze-gba-wifi-link.py")
SPEC = importlib.util.spec_from_file_location("replicated_log_analyzer", SCRIPT)
assert SPEC and SPEC.loader
ANALYZER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ANALYZER
SPEC.loader.exec_module(ANALYZER)


def log(
    role: int,
    timeout: int = 0,
    fps_milli: int = 59_730,
    fixture: bool = True,
    transfers: int = 54_000,
    lead0: int = 1,
    lead1: int = 0,
    wait_free_ppm: int = 995_000,
    input_p95_us: int = 7_000,
    input_max_us: int = 12_000,
    calibration: bool = True,
    waited: int = 540,
    input_total_us: int = 100_000,
    deadline_misses: int = 0,
    clock_failures: int = 0,
) -> str:
    trace0 = "01" * 32
    trace1 = "ab" * 32
    lines = [
            *( [
                f"calibration P{role} provisional=17 generation=19 samples=24",
                f"cal-rtt P{role} s=17 min=1000us p50=2000us "
                "p95=3000us max=4000us",
                f"cal-select P{role} s=17 selector=1 floor=1 "
                "range=1-8 delay=1 reason=1",
                f"cal-digest-a P{role} s=17 d={'12' * 16}",
                f"cal-digest-b P{role} s=17 d={'12' * 16}",
            ] if calibration else [] ),
            f"periodic P{role} f=108000 pkt=110000/109999 "
            f"B=12500000/12499920 chk=1799 sio={transfers}/{transfers * 2}",
            f"periodic P{role} rv=120/840ms max=18 q=7 in=4/4 "
            f"lead={lead0}/{lead1} "
            "audio=59250000/107999/0 wait=1000 run=80000000/79000000",
            f"periodic P{role} trace0={trace0}",
            f"periodic P{role} trace1={trace1}",
            f"periodic timing P{role} elapsed=1808135ms "
            f"fps-milli={fps_milli} rv-p50=5ms rv-p95=12ms rv-max=18ms",
            f"periodic input-wait P{role} released=108000 waited={waited} "
            f"wait-free-ppm={wait_free_ppm}",
            f"periodic input-tail P{role} p95={input_p95_us}us "
            f"max={input_max_us}us total={input_total_us}us",
            f"periodic input-health P{role} deadline-miss={deadline_misses} "
            f"clock-failure={clock_failures}",
            f"periodic poll-send P{role} count=108000 avg=40us max=100us",
            f"periodic input-lead-frame P{role} inserts=108002/108002 "
            "frames-avg=1/1 frames-max=1/1",
            f"periodic input-lead-time P{role} "
            "us-avg=16742/16742 us-max=16742/16742",
        ]
    if fixture:
        lines.append(
            f"periodic fixture P{role} status=00000003/00000003 "
            "transfers=53993/53993 errors=0/0 "
            f"timeouts=0/{timeout} lines=00036009/0007601d"
        )
    return "\n".join(lines)


def main() -> int:
    # Stock Android RetroArch retained at most 135 characters from these
    # status records during physical qualification.
    bounded_prefixes = (
        "calibration ",
        "cal-rtt ",
        "cal-select ",
        "cal-digest-",
        "periodic input-wait ",
        "periodic input-tail ",
        "periodic input-health ",
        "periodic poll-send ",
        "periodic input-lead-frame ",
        "periodic input-lead-time ",
    )
    for line in log(0).splitlines():
        if line.startswith(bounded_prefixes):
            assert len("Status: GBA Wi-Fi Link: " + line) <= 135

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        host_path = root / "host.log"
        client_path = root / "client.log"
        baseline_path = root / "baseline.log"
        host_path.write_text(log(0))
        client_path.write_text(log(1))
        baseline_path.write_text(
            "replicated-pair diagnostic: frames=108000 transfers=54000\n"
        )
        host = ANALYZER.parse(host_path)
        client = ANALYZER.parse(client_path)
        baseline = ANALYZER.parse_baseline(baseline_path)
        assert not ANALYZER.validate(host, client, 108000, baseline)
        assert not ANALYZER.validate(
            host,
            client,
            108000,
            baseline,
            expected_policy=1,
            expected_delay=1,
            one_frame_gate=True,
        )

        client_path.write_text(log(1).replace("cal-rtt P1 s=17", "cal-rtt P1 s=18"))
        try:
            ANALYZER.parse(client_path)
        except ValueError as error:
            assert "calibration session changed" in str(error)
        else:
            raise AssertionError("mixed calibration component sessions were accepted")

        client_path.write_text(log(1, input_max_us=16_744))
        errors = ANALYZER.validate(
            host,
            ANALYZER.parse(client_path),
            108000,
            baseline,
            expected_policy=1,
            expected_delay=1,
            one_frame_gate=True,
        )
        assert any("input-wait maximum" in error for error in errors)

        client_path.write_text(log(1, calibration=False))
        errors = ANALYZER.validate(
            host,
            ANALYZER.parse(client_path),
            108000,
            baseline,
            expected_policy=1,
        )
        assert any("calibration summary is missing" in error for error in errors)

        host_path.write_text(
            log(
                0,
                waited=0,
                wait_free_ppm=1_000_000,
                input_p95_us=0,
                input_max_us=0,
                input_total_us=0,
            )
        )
        client_path.write_text(
            log(
                1,
                waited=0,
                wait_free_ppm=1_000_000,
                input_p95_us=0,
                input_max_us=0,
                input_total_us=0,
            )
        )
        assert not ANALYZER.validate(
            ANALYZER.parse(host_path),
            ANALYZER.parse(client_path),
            108000,
            baseline,
            expected_policy=1,
            expected_delay=1,
            one_frame_gate=True,
        )

        client_path.write_text(log(1, input_p95_us=13_000, input_max_us=12_000))
        errors = ANALYZER.validate(
            ANALYZER.parse(host_path), ANALYZER.parse(client_path), 108000, baseline
        )
        assert any("percentiles are malformed" in error for error in errors)

        client_path.write_text(log(1, deadline_misses=1, clock_failures=1))
        errors = ANALYZER.validate(
            ANALYZER.parse(host_path), ANALYZER.parse(client_path), 108000, baseline
        )
        assert any("input deadline miss" in error for error in errors)
        assert any("telemetry clock failure" in error for error in errors)

        client_path.write_text(log(1, timeout=1, fps_milli=58_999))
        errors = ANALYZER.validate(
            host, ANALYZER.parse(client_path), 108000, baseline
        )
        assert any("58.999 FPS" in error for error in errors)
        assert any("transfer timeout" in error for error in errors)

        host_path.write_text(log(0, fixture=False))
        client_path.write_text(log(1, fixture=False))
        commercial_host = ANALYZER.parse(host_path)
        commercial_client = ANALYZER.parse(client_path)
        assert not ANALYZER.validate(
            commercial_host,
            commercial_client,
            108000,
            None,
            require_fixture=False,
        )
        errors = ANALYZER.validate(
            commercial_host, commercial_client, 108000, None
        )
        assert any("missing the continuous-fixture" in error for error in errors)

        host_path.write_text(log(0, fixture=False, transfers=0))
        errors = ANALYZER.validate(
            ANALYZER.parse(host_path),
            commercial_client,
            108000,
            None,
            require_fixture=False,
        )
        assert any("no MULTI transfers" in error for error in errors)

        host_path.write_text(
            log(0, fixture=False)
            + "\nStatus: GBA Wi-Fi Link: session failed: reason=8\n"
        )
        errors = ANALYZER.validate(
            ANALYZER.parse(host_path),
            commercial_client,
            108000,
            None,
            require_fixture=False,
        )
        assert any("explicit replicated-link failure" in error for error in errors)

        host_path.write_text(log(0, fixture=False, lead0=2))
        errors = ANALYZER.validate(
            ANALYZER.parse(host_path),
            commercial_client,
            108000,
            None,
            require_fixture=False,
        )
        assert any("frame-lead counters differ" in error for error in errors)
    print("GBA Wi-Fi Link analyzer smoke test: pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
