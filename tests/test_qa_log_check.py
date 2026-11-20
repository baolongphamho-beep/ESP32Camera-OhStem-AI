"""tools/qa_log_check.py: cycle parsing and the 6 Plan 2 QA sign-off checks (TV5-5)."""
from pathlib import Path
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import qa_log_check as q  # noqa: E402

BOOT = "[BOOT] Plan 2 servo sweep + scale + BMI"


def sweep(phase, ms, notice=None, servo=0.0):
    n = f" notice={notice}" if notice else ""
    return f'[SWEEP] -> {phase}{n} servo={servo:.1f} tilt=90.0 ms={ms & 0xFFFFFFFF} | LCD "x" / "y"'


def cycle(t0, height=170.0, hold_ms=12000, off_after=2000, servo=84.0, notices=()):
    """One normal measurement starting at t0 (ms, wraps like millis())."""
    lines = [f"[SCALE] on kg=65.00 ms={t0 & 0xFFFFFFFF}", sweep("WAKING_HOMING", t0)]
    lines.append(sweep("SERVO_SWEEP", t0 + 800))
    t = t0 + 900
    for n in notices:
        lines.append(sweep("SERVO_SWEEP", t, n, servo))
        t += 100
        lines.append(sweep("SERVO_SWEEP", t, None, servo))
    lines.append(sweep("LOCKED_MEASURE", t0 + 8000, servo=servo))
    lines.append(sweep("RESULT_HOLD", t0 + 9600, servo=servo))
    lines.append(f"[RESULT] H={height:.1f} W=65.00 BMI=22.49 D=2.80 servo={servo:.1f} "
                 f"ms={(t0 + 9600) & 0xFFFFFFFF}")
    off = t0 + 9600 + off_after
    lines.append(f"[SCALE] off kg=0.00 ms={off & 0xFFFFFFFF}")
    lines.append(sweep("STANDBY", off + hold_ms))
    return lines


def checks_for(lines, **kw):
    rep = q.parse_log(lines)
    return rep, {c.name: c for c in q.evaluate(rep, **kw)}


class ParseTests(unittest.TestCase):
    def test_normal_cycle_timings(self):
        rep = q.parse_log([BOOT] + cycle(10000))
        self.assertEqual(rep.boots, 1)
        self.assertEqual(len(rep.cycles), 1)
        c = rep.cycles[0]
        self.assertEqual(c.outcome, "result")
        r = c.row()
        self.assertEqual(r["wake_ms"], "0")
        self.assertEqual(r["homing_ms"], "800")
        self.assertEqual(r["sweep_ms"], "7200")
        self.assertEqual(r["to_result_ms"], "9600")
        self.assertEqual(r["hold_ms"], "12000")
        self.assertEqual(r["height_cm"], "170.0")
        self.assertEqual(r["stop_servo_deg"], "84.0")

    def test_retry_after_too_close_stays_one_cycle(self):
        lines = [BOOT, "[SCALE] on kg=70.00 ms=1000", sweep("WAKING_HOMING", 1000),
                 sweep("SERVO_SWEEP", 1800), sweep("SERVO_SWEEP", 9000, "TooClose", 60),
                 sweep("WAKING_HOMING", 12000), sweep("SERVO_SWEEP", 12800),
                 sweep("RESULT_HOLD", 20000, servo=84), "[SCALE] off kg=0.00 ms=21000",
                 sweep("STANDBY", 33000)]
        rep = q.parse_log(lines)
        self.assertEqual(len(rep.cycles), 1)
        self.assertEqual(rep.cycles[0].notices, ["TooClose"])
        self.assertEqual(rep.cycles[0].outcome, "height_only")  # No [RESULT] line = no BMI.

    def test_next_person_during_hold_starts_new_cycle(self):
        lines = [BOOT] + cycle(1000)[:-2] + [
            "[SCALE] off kg=0.00 ms=12000", "[SCALE] on kg=80.00 ms=15000", sweep("WAKING_HOMING", 15000)]
        rep = q.parse_log(lines)
        self.assertEqual(len(rep.cycles), 2)
        self.assertTrue(rep.cycles[0].replaced)
        self.assertIsNone(rep.cycles[0].hold_ms)  # Cut short on purpose: not a QA5 failure.
        self.assertEqual(rep.cycles[1].scale_on, 15000)
        self.assertEqual(rep.cycles[1].outcome, "open")

    def test_millis_wrap(self):
        rep = q.parse_log([BOOT] + cycle(0xFFFFFFFF - 3000))
        self.assertEqual(rep.cycles[0].row()["to_result_ms"], "9600")
        self.assertEqual(rep.cycles[0].hold_ms, 12000)

    def test_binary_noise_and_heap(self):
        lines = [BOOT, "SSV1\x00\x07\xff garbage {\"kind\":\"status\"}", "[TASK] BLE: ok; free heap=180000",
                 "[TASK] BLE: ok; free heap=150000"] + cycle(5000)
        rep = q.parse_log(lines)
        self.assertEqual(rep.min_heap, 150000)
        self.assertEqual(len(rep.cycles), 1)


class EvaluateTests(unittest.TestCase):
    def test_full_session_passes(self):
        lines = [BOOT]
        t = 1000
        for i in range(15):
            notices = ("TooClose",) if i == 3 else (("Multiple",) if i == 5 else ())
            lines += cycle(t, height=150 + i * 3, servo=76 + (i % 4) * 4, notices=notices)
            t += 40000
        lines += ["[SCALE] on kg=60.00 ms=%d" % t, sweep("WAKING_HOMING", t), sweep("SERVO_SWEEP", t + 800),
                  "[SCALE] off kg=0.00 ms=%d" % (t + 2000), sweep("STANDBY", t + 4000)]
        _, ch = checks_for(lines)
        self.assertTrue(all(c.status == "PASS" for c in ch.values()), {k: v.detail for k, v in ch.items()})

    def test_brownout_fails_qa6(self):
        lines = [BOOT] + cycle(1000) + ["Brownout detector was triggered", "ESP-ROM:esp32s3", BOOT]
        _, ch = checks_for(lines)
        self.assertEqual(ch["QA6 no brownout/reset"].status, "FAIL")

    def test_silent_reboot_fails_qa6_and_qa4(self):
        lines = [BOOT, "[SCALE] on kg=65.00 ms=1000", sweep("WAKING_HOMING", 1000), BOOT] + cycle(3000)
        rep, ch = checks_for(lines)
        self.assertEqual(rep.boots, 2)
        self.assertEqual(ch["QA6 no brownout/reset"].status, "FAIL")
        self.assertEqual(ch["QA4 early step-off"].status, "FAIL")  # First cycle never closed.

    def test_short_hold_fails_qa5(self):
        _, ch = checks_for([BOOT] + cycle(1000, hold_ms=4000))
        self.assertEqual(ch["QA5 result hold"].status, "FAIL")

    def test_slow_wake_fails_qa1(self):
        lines = [BOOT, "[SCALE] on kg=65.00 ms=1000"] + cycle(3000)[1:]
        _, ch = checks_for(lines)
        self.assertEqual(ch["QA1 wake (firmware part)"].status, "FAIL")

    def test_same_angle_fails_qa2(self):
        lines = [BOOT] + cycle(1000, servo=84) + cycle(50000, height=190, servo=84)
        _, ch = checks_for(lines)
        self.assertEqual(ch["QA2 several heights"].status, "FAIL")

    def test_missing_data_is_not_pass(self):
        _, ch = checks_for([BOOT])
        self.assertTrue(all(c.status == "N/A" for c in ch.values()))

    def test_cli_exit_code_and_csv(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / "run.log"
            out = Path(d) / "run.csv"
            log.write_text("\n".join([BOOT] + cycle(1000, hold_ms=3000)), encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                code = q.main([str(log), "--csv", str(out)])
            self.assertEqual(code, 1)
            rows = out.read_text(encoding="utf-8").splitlines()
            self.assertEqual(rows[0].split(","), q.CSV_FIELDS)
            self.assertEqual(len(rows), 2)

    def test_reads_utf16_powershell_log(self):
        # Windows PowerShell 5.1 "| Tee-Object -FilePath x.log" writes UTF-16 LE with BOM.
        with tempfile.TemporaryDirectory() as d:
            for enc in ("utf-16", "utf-8-sig", "utf-8"):
                log = Path(d) / f"run_{enc}.log"
                log.write_text("\r\n".join([BOOT] + cycle(1000)), encoding=enc)
                rep = q.parse_log(q.read_log_lines(str(log)))
                self.assertEqual((rep.boots, len(rep.cycles), rep.cycles[0].outcome), (1, 1, "result"), enc)


class FirmwareFormatTests(unittest.TestCase):
    """The regexes must follow the printf formats in the firmware and in the simulator."""

    def test_format_strings_match(self):
        paths = [ROOT / "tests" / "qa_log_sim.cpp"]
        firmware = ROOT / "src" / "app_main.cpp"
        # The TV5 contribution can be tested before the Arduino integration is
        # included. Always check the simulator; check real firmware when present.
        if firmware.is_file():
            paths.append(firmware)
        for path in paths:
            src = path.read_text(encoding="utf-8")
            self.assertIn('"[SCALE] %s kg=%.2f ms=%lu\\n"', src, path.name)
            self.assertIn('[SWEEP] -> %s%s%s servo=%.1f tilt=%.1f ms=%lu', src, path.name)
            self.assertIn('"[RESULT] H=%.1f W=%.2f BMI=%.2f D=%.2f servo=%.1f ms=%lu\\n"', src, path.name)
            self.assertIn("[BOOT] Plan 2", src, path.name)


if __name__ == "__main__":
    unittest.main()
