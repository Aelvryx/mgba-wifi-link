#!/usr/bin/env python3
"""Deterministic smoke tests for analyze-replicated-netplay.py."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("analyze-replicated-netplay.py")
SPEC = importlib.util.spec_from_file_location("replicated_log_analyzer", SCRIPT)
assert SPEC and SPEC.loader
ANALYZER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ANALYZER
SPEC.loader.exec_module(ANALYZER)


def log(role: int, timeout: int = 0, fps_milli: int = 59_730) -> str:
    trace0 = "01" * 32
    trace1 = "ab" * 32
    return "\n".join(
        [
            f"periodic P{role} f=108000 pkt=110000/109999 "
            "B=12500000/12499920 chk=1799 sio=54000/108000",
            f"periodic P{role} rv=120/840ms max=18 q=7 in=4/4 "
            "audio=59250000/107999/0 wait=1000 run=80000000/79000000",
            f"periodic P{role} trace0={trace0}",
            f"periodic P{role} trace1={trace1}",
            f"periodic timing P{role} elapsed=1808135ms "
            f"fps-milli={fps_milli} rv-p50=5ms rv-p95=12ms rv-max=18ms",
            f"periodic fixture P{role} status=00000003/00000003 "
            "transfers=53993/53993 errors=0/0 "
            f"timeouts=0/{timeout} lines=00036009/0007601d",
        ]
    )


def main() -> int:
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

        client_path.write_text(log(1, timeout=1, fps_milli=58_999))
        errors = ANALYZER.validate(
            host, ANALYZER.parse(client_path), 108000, baseline
        )
        assert any("58.999 FPS" in error for error in errors)
        assert any("transfer timeout" in error for error in errors)
    print("replicated netplay analyzer smoke test: pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
