"""Native metadata QA must never turn missing field evidence into sign-off."""
import contextlib
import io
import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "camera"))
from native_qa import analyze, load_records, main
from telemetry import PacketRecorder, power_lines, sleep_status
from usb_protocol import packet


def row(at, kind="status", **meta):
    return {"host_ms": at, "meta": {"kind": kind, **meta}}


class NativeQATests(unittest.TestCase):
    def test_legacy_preview_speed_does_not_sign_off_scale_qa(self):
        rows = [row(0, phase="CAMERA_TEST", preview=True),
                row(1000, "frame", phase="CAMERA_TEST", processing_ms=325, capture_ms=5284),
                row(2000, "frame", phase="CAMERA_TEST", processing_ms=327, capture_ms=488)]
        result, _ = analyze(rows)
        self.assertEqual(result["metrics_ms"]["processing_ms"]["median"], 326)
        checks = {c["name"]: c["status"] for c in result["checks"]}
        self.assertEqual(checks["observed_processing"], "PASS")
        self.assertEqual(checks["observed_capture"], "N/A")
        self.assertEqual(checks["frame_session_isolation"], "N/A")
        self.assertTrue(all(c["status"] == "N/A" for c in result["checks"] if c["name"].startswith("QA")))

    def test_sessions_and_recording_boundaries_reject_old_packets(self):
        rows = [row(0, "scale", session_id=7, phase="ACTIVE"),
                row(10, "frame", session_id=7, processing_ms=325),
                row(15, "scale", session_id=8, phase="DRAINING"),
                row(20, "frame", session_id=7, processing_ms=1),
                {"kind": "recording_start"},
                row(0, "frame", session_id=0, processing_ms=330)]
        result, exported = analyze(rows)
        self.assertEqual(result["frames"], 2)
        self.assertFalse(exported[3]["accepted"])
        self.assertEqual(result["metrics_ms"]["processing_ms"]["min"], 325)
        self.assertEqual(next(c["status"] for c in result["checks"] if c["name"] == "frame_session_isolation"), "FAIL")

    def test_phase_hold_duration_is_not_physical_unload_timer(self):
        result, _ = analyze([row(0, phase="RESULT_HOLD"), row(12000, phase="STANDBY")])
        self.assertEqual(result["observed_hold_phase_ms"], [12000])
        self.assertEqual(next(c["status"] for c in result["checks"] if c["name"].startswith("QA5")), "N/A")

    def test_empty_data_bad_times_and_thresholds(self):
        result, _ = analyze([row(100, "frame", processing_ms=True), row(50, processing_ms=-1)])
        self.assertEqual(result["metrics_ms"]["processing_ms"]["samples"], 0)
        self.assertEqual(result["checks"][0]["status"], "FAIL")
        result, _ = analyze([row(0, "frame", processing_ms=450, capture_ms=600)], capture_max_ms=500)
        checks = {c["name"]: c["status"] for c in result["checks"]}
        self.assertEqual(checks["observed_processing"], "FAIL")
        self.assertEqual(checks["observed_capture"], "FAIL")

    def test_crc_binary_jsonl_report_csv_and_safe_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            binary = root / "capture.bin"
            bad = bytearray(packet({"kind": "frame", "processing_ms": 1}, b"JPEG"))
            bad[-1] ^= 1
            binary.write_bytes(b"boot noise" + bad + packet({"kind": "frame", "processing_ms": 325}, b"JPEG"))
            rows, source = load_records(binary)
            self.assertEqual(len(rows), 1)
            self.assertNotIn("host_ms", rows[0])
            self.assertIn("CRC-valid", source)
            log = root / "capture.jsonl"
            log.write_text(json.dumps(row(0, "frame", processing_ms=325)) + "\n", encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(main([str(log), "--json", str(root / "summary.json"), "--csv", str(root / "packets.csv")]), 0)
                self.assertEqual(main([str(log), "--json", str(log)]), 2)
            report = json.loads((root / "summary.json").read_text(encoding="utf-8"))
            self.assertEqual(report["frames"], 1)
            self.assertIn("processing_ms", (root / "packets.csv").read_text(encoding="utf-8-sig"))
            log.write_text('{"packets":4,"last_frame":{}}', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "probe summary"):
                load_records(log)
            log.write_text("invalid", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "line 1"):
                load_records(log)


class TelemetryTests(unittest.TestCase):
    def test_missing_or_configured_sleep_is_not_actual_sleep(self):
        self.assertIn("unknown", sleep_status({}))
        self.assertEqual(sleep_status({"light_sleep_enabled": False}), "not configured")
        self.assertIn("blocked by USB", sleep_status({"light_sleep_enabled": True, "usb_sleep_blocked": True}))
        self.assertIn("PM locks", sleep_status({"light_sleep_enabled": True, "no_ls_locks": 1}))
        self.assertIn("unmeasured", sleep_status({"light_sleep_enabled": True, "usb_sleep_blocked": False, "no_ls_locks": 0}))
        self.assertIn("unknown", sleep_status({"light_sleep_enabled": True, "no_ls_locks": "bad"}))
        self.assertIn("state unknown", power_lines({"wifi_ps": []})[0])
        self.assertIn("not started", power_lines({"wifi_started": False})[0])

    def test_recorder_append_runs_no_secrets_images_or_nan(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "metadata.jsonl"
            times = iter((10, 10.5))
            with PacketRecorder(path, clock=lambda: next(times)) as log:
                log.write({"kind": "frame", "password": "SECRET", "ssid": "PRIVATE",
                           "message": "SECRET", "processing_ms": 325, "bmi": float('nan'),
                           "boxes": [[0, 0, 20, 20, .9]]}, 100)
            with PacketRecorder(path, clock=lambda: 20):
                pass
            content = path.read_text(encoding="utf-8")
            self.assertNotIn("SECRET", content)
            self.assertNotIn("PRIVATE", content)
            self.assertNotIn("NaN", content)
            rows = [json.loads(line) for line in content.splitlines()]
            self.assertEqual(rows[1]["host_ms"], 500)
            self.assertEqual(rows[1]["meta"]["people_count"], 1)
            self.assertNotIn("boxes", rows[1]["meta"])
            self.assertEqual(rows[2]["kind"], "recording_start")


if __name__ == "__main__":
    unittest.main()
