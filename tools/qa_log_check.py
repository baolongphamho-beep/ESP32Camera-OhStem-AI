"""TV5-5 QA: read a serial log of env yolo_uno_servo_sweep and check the Plan 2 sign-off.

Capture a log (USB, 115200):
    pio device monitor -e yolo_uno_servo_sweep | Tee-Object -FilePath qa_run1.log
Then:
    py -3 tools/qa_log_check.py qa_run1.log --csv qa_run1.csv

It uses the "ms=" lines the firmware prints:
    [SCALE] on kg=65.20 ms=12345
    [SWEEP] -> WAKING_HOMING servo=0.0 tilt=90.0 ms=12350 | LCD "..." / "..."
    [RESULT] H=170.2 W=65.20 BMI=22.51 D=2.85 servo=84.0 ms=20000
Times come from millis() on the board, so the delay between your foot touching the
scale and the first BLE packet is NOT included: measure that with a stopwatch/video.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from dataclasses import dataclass, field
from typing import Iterable, List, Optional

SWEEP_RE = re.compile(r"\[SWEEP\] -> (\w+)(?: notice=(\w+))? servo=(-?[\d.]+) tilt=(-?[\d.]+) ms=(\d+)")
SCALE_RE = re.compile(r"\[SCALE\] (on|off) kg=(-?[\d.]+|nan|inf) ms=(\d+)")
RESULT_RE = re.compile(
    r"\[RESULT\] H=([\d.]+) W=([\d.]+) BMI=([\d.]+) D=([\d.]+) servo=(-?[\d.]+) ms=(\d+)")
BOOT_RE = re.compile(r"\[BOOT\] Plan 2")
CRASH_RE = re.compile(r"Brownout|BROWNOUT|Guru Meditation|abort\(\) was called|rst:0x|Backtrace:", re.I)
HEAP_RE = re.compile(r"free heap=(\d+)")

CSV_FIELDS = [
    "cycle", "boot", "outcome", "notices", "height_cm", "weight_kg", "bmi", "distance_m",
    "stop_servo_deg", "wake_ms", "homing_ms", "sweep_ms", "to_result_ms", "hold_ms",
]


def _dt(a: Optional[int], b: Optional[int]) -> Optional[int]:
    """b - a on a 32-bit millis() clock (survives the 49-day wrap)."""
    if a is None or b is None:
        return None
    return (b - a) & 0xFFFFFFFF


@dataclass
class Cycle:
    index: int
    boot: int
    scale_on: Optional[int] = None
    wake: Optional[int] = None
    sweep: Optional[int] = None
    locked: Optional[int] = None
    result_phase: Optional[int] = None
    last_off: Optional[int] = None
    standby: Optional[int] = None
    notices: List[str] = field(default_factory=list)
    height: Optional[float] = None
    weight: Optional[float] = None
    bmi: Optional[float] = None
    distance: Optional[float] = None
    stop_servo: Optional[float] = None
    replaced: bool = False  # RESULT_HOLD cut short by the next person (hold not measured).

    @property
    def outcome(self) -> str:
        if self.bmi is not None:
            return "result"
        if self.result_phase is not None:
            return "height_only"
        if self.standby is not None:
            return "aborted"
        return "open"  # Log ended (or board reset) before STANDBY.

    @property
    def hold_ms(self) -> Optional[int]:
        # RESULT_HOLD ends hold_sec after the last moment someone was on the scale.
        if self.result_phase is None or self.standby is None or self.replaced:
            return None
        start = self.last_off if self.last_off is not None else self.result_phase
        return _dt(start, self.standby)

    def row(self) -> dict:
        def fmt(v, digits=2):
            return "" if v is None else (f"{v:.{digits}f}" if isinstance(v, float) else str(v))
        return {
            "cycle": self.index, "boot": self.boot, "outcome": self.outcome,
            "notices": "+".join(self.notices),
            "height_cm": fmt(self.height, 1), "weight_kg": fmt(self.weight), "bmi": fmt(self.bmi),
            "distance_m": fmt(self.distance), "stop_servo_deg": fmt(self.stop_servo, 1),
            "wake_ms": fmt(_dt(self.scale_on, self.wake)), "homing_ms": fmt(_dt(self.wake, self.sweep)),
            "sweep_ms": fmt(_dt(self.sweep, self.locked)),
            "to_result_ms": fmt(_dt(self.wake, self.result_phase)), "hold_ms": fmt(self.hold_ms),
        }


@dataclass
class Report:
    cycles: List[Cycle]
    boots: int
    crashes: List[str]
    min_heap: Optional[int]


def parse_log(lines: Iterable[str]) -> Report:
    cycles: List[Cycle] = []
    crashes: List[str] = []
    boots = 0
    min_heap: Optional[int] = None
    current: Optional[Cycle] = None
    pending_on: Optional[int] = None  # Last [SCALE] on not yet used by a cycle.

    for raw in lines:
        line = raw.rstrip("\r\n")
        if BOOT_RE.search(line):
            boots += 1
            current, pending_on = None, None  # A reboot ends any open cycle.
            continue
        if CRASH_RE.search(line):
            crashes.append(line.strip())
        m = HEAP_RE.search(line)
        if m:
            heap = int(m.group(1))
            min_heap = heap if min_heap is None else min(min_heap, heap)

        m = SCALE_RE.search(line)
        if m:
            on, ms = m.group(1) == "on", int(m.group(3))
            if on:
                pending_on = ms  # Used by the next WAKING_HOMING (from STANDBY or RESULT_HOLD).
            else:
                pending_on = None
                if current is not None:
                    current.last_off = ms
            continue

        m = RESULT_RE.search(line)
        if m and current is not None:
            current.height, current.weight, current.bmi, current.distance, current.stop_servo = (
                float(m.group(i)) for i in range(1, 6))
            continue

        m = SWEEP_RE.search(line)
        if not m:
            continue
        phase, notice, servo, ms = m.group(1), m.group(2), float(m.group(3)), int(m.group(5))
        if phase == "WAKING_HOMING" and (current is None or current.result_phase is not None
                                         or current.standby is not None):
            # New cycle: from STANDBY, or a new person during RESULT_HOLD.
            if current is not None and current.standby is None:
                current.standby = ms  # Previous result replaced by the next person.
                current.replaced = True
            current = Cycle(index=len(cycles) + 1, boot=boots, scale_on=pending_on, wake=ms)
            cycles.append(current)
            pending_on = None
        if current is None:
            continue
        if notice and notice not in current.notices:
            current.notices.append(notice)
        if phase == "SERVO_SWEEP" and current.sweep is None:
            current.sweep = ms
        elif phase == "LOCKED_MEASURE" and current.locked is None:
            current.locked = ms
        elif phase == "RESULT_HOLD" and current.result_phase is None:
            current.result_phase = ms
            if current.stop_servo is None:
                current.stop_servo = servo
        elif phase == "STANDBY":
            current.standby = ms
            current = None

    return Report(cycles, boots, crashes, min_heap)


@dataclass
class Check:
    name: str
    status: str  # PASS / FAIL / N/A (not enough data in this log)
    detail: str


def evaluate(rep: Report, wake_max_ms: int = 1500, hold_min_s: float = 10, hold_max_s: float = 15,
             min_cycles: int = 15) -> List[Check]:
    checks: List[Check] = []
    wakes = [_dt(c.scale_on, c.wake) for c in rep.cycles if c.scale_on is not None]
    if wakes:
        worst = max(wakes)
        checks.append(Check("QA1 wake (firmware part)", "PASS" if worst <= wake_max_ms else "FAIL",
                            f"worst {worst} ms over {len(wakes)} cycle(s), limit {wake_max_ms} ms"))
    else:
        checks.append(Check("QA1 wake (firmware part)", "N/A", "no [SCALE] on -> WAKING_HOMING pair"))

    results = [c for c in rep.cycles if c.outcome == "result"]
    servos = sorted({round(c.stop_servo, 1) for c in results if c.stop_servo is not None})
    if len(results) >= 2:
        checks.append(Check("QA2 several heights", "PASS" if len(servos) >= 2 else "FAIL",
                            f"{len(results)} results, stop angles {servos} (compare H with tape measure)"))
    else:
        checks.append(Check("QA2 several heights", "N/A", f"{len(results)} result(s); need >= 2 people"))

    seen = {n for c in rep.cycles for n in c.notices}
    for name, notice in (("QA3 too close", "TooClose"), ("QA3 two people", "Multiple")):
        checks.append(Check(name, "PASS" if notice in seen else "N/A",
                            "notice seen" if notice in seen else f"no {notice} notice in this log"))

    aborted = [c for c in rep.cycles if c.outcome == "aborted"]
    # An open cycle that is not the last one was cut by a reboot (also reported by QA6).
    stuck = [c for c in rep.cycles[:-1] if c.outcome == "open"]
    if aborted or stuck:
        checks.append(Check("QA4 early step-off", "FAIL" if stuck else "PASS",
                            f"{len(aborted)} cycle(s) back to STANDBY before a result, "
                            f"{len(stuck)} cycle(s) never closed"))
    else:
        checks.append(Check("QA4 early step-off", "N/A", "no cycle left before the result"))

    holds = [c.hold_ms for c in results if c.hold_ms is not None and c.last_off is not None]
    if holds:
        bad = [h for h in holds if not hold_min_s * 1000 <= h <= hold_max_s * 1000 + 500]
        checks.append(Check("QA5 result hold", "FAIL" if bad else "PASS",
                            f"hold {min(holds)}-{max(holds)} ms over {len(holds)} result(s), "
                            f"want {hold_min_s:g}-{hold_max_s:g} s"))
    else:
        checks.append(Check("QA5 result hold", "N/A", "no result followed by step-off and STANDBY"))

    closed = [c for c in rep.cycles if c.outcome != "open"]
    if rep.crashes or rep.boots > 1:
        checks.append(Check("QA6 no brownout/reset", "FAIL",
                            f"{rep.boots} boot(s), {len(rep.crashes)} crash line(s): "
                            + (rep.crashes[0] if rep.crashes else "board rebooted")))
    elif len(closed) >= min_cycles:
        checks.append(Check("QA6 no brownout/reset", "PASS", f"{len(closed)} cycles, 1 boot, no crash line"))
    else:
        checks.append(Check("QA6 no brownout/reset", "N/A",
                            f"only {len(closed)} closed cycle(s), need {min_cycles}"))
    return checks


def read_log_lines(path: str) -> List[str]:
    """Windows PowerShell 5.1 Tee-Object/Out-File write UTF-16; PlatformIO log2file writes UTF-8."""
    with open(path, "rb") as source:
        data = source.read()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):
        text = data.decode("utf-16", errors="replace")
    else:
        text = data.decode("utf-8-sig", errors="replace")
    return text.splitlines()


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", help="serial log file of yolo_uno_servo_sweep")
    ap.add_argument("--csv", help="write one row per cycle (see docs/QA_CHECKLIST.md)")
    ap.add_argument("--wake-max-ms", type=int, default=1500)
    ap.add_argument("--hold-min", type=float, default=10, help="seconds")
    ap.add_argument("--hold-max", type=float, default=15, help="seconds")
    ap.add_argument("--min-cycles", type=int, default=15, help="cycles needed for QA6")
    args = ap.parse_args(argv)

    rep = parse_log(read_log_lines(args.log))

    print(f"Boots: {rep.boots}   cycles: {len(rep.cycles)}   min free heap: {rep.min_heap or '-'}")
    print(f"{'#':>3} {'outcome':<11} {'H cm':>6} {'W kg':>6} {'BMI':>5} {'servo':>5} "
          f"{'wake':>5} {'sweep':>6} {'total':>6} {'hold':>6}  notices")
    for c in rep.cycles:
        r = c.row()
        print(f"{r['cycle']:>3} {r['outcome']:<11} {r['height_cm']:>6} {r['weight_kg']:>6} {r['bmi']:>5} "
              f"{r['stop_servo_deg']:>5} {r['wake_ms']:>5} {r['sweep_ms']:>6} {r['to_result_ms']:>6} "
              f"{r['hold_ms']:>6}  {r['notices']}")

    checks = evaluate(rep, args.wake_max_ms, args.hold_min, args.hold_max, args.min_cycles)
    print()
    for ch in checks:
        print(f"[{ch.status:<4}] {ch.name}: {ch.detail}")

    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=CSV_FIELDS)
            w.writeheader()
            for c in rep.cycles:
                w.writerow(c.row())
        print(f"\nCSV: {args.csv}")
    return 1 if any(ch.status == "FAIL" for ch in checks) else 0


if __name__ == "__main__":
    sys.exit(main())
