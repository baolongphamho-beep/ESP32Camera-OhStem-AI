"""camera/usb_viewer.py: safe parsing of old/new/broken frame JSON, overlay text,
--configure sections and --set. No serial port or window is opened."""
import pathlib
import re
import sys
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "camera"))
import usb_viewer as v  # noqa: E402

OLD_FRAME = {"kind": "frame", "status": "Measured", "boxes": [[10, 20, 110, 220, 0.9]],
             "height_cm": 171.24, "distance_m": 2.8, "weight_kg": 65.3, "weight_stable": True,
             "bmi": 22.27, "processing_ms": 812}
NEW_FRAME = dict(OLD_FRAME, phase="LOCKED_MEASURE", servo_deg=97.5, tilt_deg=7.5,
                 framing="FullBody", margin_px=8)


class PureHelperTests(unittest.TestCase):
    def test_safe_boxes_skips_malformed_entries(self):
        raw = [[10, 20, 110, 220, 0.9],           # ok
               [1, 2, 3, 4],                        # ok, no confidence
               None, "box", {"x": 1}, [1, 2, 3],    # wrong shapes
               [1, 2, float("nan"), 4, .5],        # NaN
               ["1", 2, 3, 4, .5], [True, 2, 3, 4], # non-numbers
               [50, 50, 40, 60, .9],               # x2 <= x1
               [1, 2, 3, 4, "high"]]               # bad confidence -> 0
        self.assertEqual(v.safe_boxes(raw), [(10, 20, 110, 220, .9), (1, 2, 3, 4, 0.0),
                                             (1, 2, 3, 4, 0.0)])
        for bad in (None, "x", 5, {"a": 1}):
            self.assertEqual(v.safe_boxes(bad), [])

    def test_fmt_handles_missing_and_wrong_types(self):
        self.assertEqual(v.fmt(171.24, 1, " cm"), "171.2 cm")
        for bad in (None, "abc", True, float("inf"), [1]):
            self.assertEqual(v.fmt(bad), "--")

    def test_framing_style(self):
        self.assertEqual(v.framing_style("FullBody"), (v.GREEN, ""))
        self.assertEqual(v.framing_style("TooClose")[0], v.RED)
        self.assertIn("Step back", v.framing_style("too_close")[1])
        for name in ("TopClipped", "BottomClipped", "SideClipped"):
            self.assertEqual(v.framing_style(name)[0], v.ORANGE)
        self.assertEqual(v.framing_style("SomethingNew"), (v.YELLOW, ""))
        self.assertEqual(v.framing_style(None), (v.GREEN, ""))
        self.assertEqual(v.framing_style(3), (v.GREEN, ""))

    def test_margin_rect(self):
        self.assertEqual(v.margin_rect(320, 240, 8), (8, 8, 312, 232))
        for bad in (None, 0, -3, "8", float("nan"), 120, 500):
            self.assertIsNone(v.margin_rect(320, 240, bad))

    def test_overlay_old_firmware_has_no_plan2_line(self):
        lines = [t for t, _ in v.overlay_lines(OLD_FRAME)]
        self.assertEqual(lines, ["Measured", "Height: 171.2 cm   Distance: 2.80 m",
                                 "Weight: 65.30 kg LOCK   BMI: 22.3", "On-board processing: 812 ms"])

    def test_overlay_new_firmware_shows_phase_servo_tilt_framing(self):
        lines = v.overlay_lines(dict(NEW_FRAME, framing="TooClose"))
        texts = [t for t, _ in lines]
        self.assertIn("Phase: LOCKED_MEASURE   Servo: 97.5 deg   Tilt: 7.5 deg", texts)
        framing = [(t, c) for t, c in lines if t.startswith("Framing:")]
        self.assertEqual(len(framing), 1)
        self.assertIn("Step back", framing[0][0])
        self.assertEqual(framing[0][1], v.RED)

    def test_preview_overlay_does_not_claim_measurements_or_servo_motion(self):
        lines = [text for text, _ in v.overlay_lines(dict(OLD_FRAME, preview=True))]
        self.assertIn("scale and servo disabled", lines[0])
        self.assertFalse(any("Height:" in text or "BMI:" in text or "Tilt:" in text for text in lines))

    def test_overlay_partial_or_broken_fields_do_not_crash(self):
        for meta in ({}, {"phase": None}, {"servo_deg": "x"}, {"framing": 5},
                     {"height_cm": "abc", "weight_stable": "yes", "status": None},
                     {"phase": "SERVO_SWEEP"}):
            with self.subTest(meta=meta):
                lines = v.overlay_lines(meta)
                self.assertTrue(all(isinstance(t, str) for t, _ in lines))
        texts = [t for t, _ in v.overlay_lines({"phase": "SERVO_SWEEP"})]
        self.assertIn("Phase: SERVO_SWEEP   Servo: --   Tilt: --", texts)  # No unit after "--".
        self.assertIn("moving", v.overlay_lines({"weight_stable": "yes"})[2][0])

    def test_status_text(self):
        self.assertEqual(v.status_text({"message": "Camera capture failed"}), "Camera capture failed")
        self.assertEqual(v.status_text({"message": "WiFi not ready", "phase": "STANDBY"}),
                         "[STANDBY] WiFi not ready")
        self.assertEqual(v.status_text({}), "Waiting")
        self.assertEqual(v.status_text({"message": 3}), "Waiting")

    def test_parse_set_args(self):
        self.assertEqual(v.parse_set_args(["servo_pin=10", " hold_sec = 12 "]),
                         {"servo_pin": "10", "hold_sec": "12"})
        self.assertEqual(v.parse_set_args(None), {})
        for bad in (["servo_pin"], ["=10"]):
            with self.assertRaises(ValueError):
                v.parse_set_args(bad)

    def test_scale_packets_do_not_invent_height_or_bmi(self):
        lines = v.scale_notice_lines({"phase": "ACTIVE", "weight_kg": 65.2,
                                      "weight_stable": True, "cpu_mhz": 240,
                                      "ble_duty_percent": 12.5})
        self.assertIn("240 MHz", lines[0])
        self.assertIn("65.20 kg", lines[1])
        self.assertIn("LOCK", lines[1])
        self.assertFalse(any("BMI" in line or "Height" in line for line in lines))

    def test_scale_hold_shows_saved_result_and_missing_values_are_unknown(self):
        lines = v.scale_notice_lines({"phase": "RESULT_HOLD", "height_cm": 180,
                                      "weight_kg": 81, "bmi": 25})
        self.assertIn("Height: 180.0 cm | BMI: 25.0", lines)
        unknown = v.scale_notice_lines({"phase": "RESULT_HOLD", "bmi": float('nan')})
        self.assertIn("Height: -- | BMI: --", unknown)
        self.assertIn("Weight: --", unknown[1])


class ConfigureTests(unittest.TestCase):
    def run_configure(self, answers):
        with mock.patch("builtins.input", side_effect=answers), \
                mock.patch("getpass.getpass", return_value="secret"), \
                mock.patch("builtins.print"):
            return v.configure()

    def test_enter_everywhere_sends_same_bytes_as_old_viewer(self):
        # ssid, url, mac, geometry? (Enter = yes), mode, distance, reference, focal, servo? (Enter = no)
        out = self.run_configure(["", "", "", "", "", "", "", "", ""])
        self.assertEqual(out, b"BEGIN\nCFG mode=fixed\nCFG distance=3.0\nCFG reference=183.0\n"
                              b"CFG focal=0.0\nCFG tilt=0\nAPPLY\n")

    def test_servo_section_only_with_retry_on_invalid_value(self):
        answers = ["", "", "70:87:9e:ea:3a:e0", "n", "y",
                   "19", "10",          # GPIO19 is USB -> asked again
                   "112", "85", "4", "90",
                   "0", "-1",           # dir 0 rejected
                   "200", "12"]
        out = self.run_configure(answers)
        self.assertEqual(out, b"BEGIN\nCFG mac=70:87:9e:ea:3a:e0\nCFG servo_pin=10\nCFG servo_bottom=112\n"
                              b"CFG servo_top=85\nCFG servo_step=4\nCFG servo_level=90\n"
                              b"CFG servo_dir=-1\nCFG servo_settle=200\nCFG hold_sec=12\nAPPLY\n")

    def test_enter_keeps_unchanged_servo_keys(self):
        answers = ["", "", "", "n", "y", "", "", "", "", "", "", "", "15"]
        self.assertEqual(self.run_configure(answers), b"BEGIN\nCFG hold_sec=15\nAPPLY\n")

    def test_bottom_equal_top_asks_servo_section_again(self):
        first = ["", "90", "90", "", "", "", "", ""]
        second = ["", "100", "80", "", "", "", "", ""]
        out = self.run_configure(["", "", "", "n", "y"] + first + second)
        self.assertEqual(out, b"BEGIN\nCFG servo_bottom=100\nCFG servo_top=80\nAPPLY\n")

    def test_servo_mode_full_plan2_setup(self):
        # TV5 DoD: a new member configures the whole Plan 2 env from --configure only.
        answers = ["", "", "70:87:9e:ea:3a:e0", "", "servo", "112", "1.3",
                   "y", "10", "0", "180", "4", "92", "1", "400", "12"]
        out = self.run_configure(answers)
        self.assertEqual(out, b"BEGIN\nCFG mac=70:87:9e:ea:3a:e0\nCFG height=112.0\nCFG focal=1.3\n"
                              b"CFG servo_pin=10\nCFG servo_bottom=0\nCFG servo_top=180\nCFG servo_step=4\n"
                              b"CFG servo_level=92\nCFG servo_dir=1\nCFG servo_settle=400\nCFG hold_sec=12\n"
                              b"APPLY\n")
        # Every key sent is one the Plan 2 firmware applies (not ignored, not unknown).
        src = (ROOT / "lib" / "EdgeSweep" / "edge_config.h").read_text(encoding="utf-8")
        applied = set(re.findall(r'strcmp\(key, "(\w+)"\)', src))
        ignored = set(re.search(r"ignored\[\] = \{([^}]*)\}", src).group(1).replace('"', "").replace(" ", "")
                      .replace("\n", "").split(","))
        sent = {line.split("=")[0][4:] for line in out.decode().splitlines() if line.startswith("CFG ")}
        self.assertTrue(sent <= applied, sent - applied)
        self.assertFalse(sent & ignored)

    def test_nothing_changed_sends_nothing(self):
        self.assertIsNone(self.run_configure(["", "", "", "n", "n"]))

    def test_wifi_password_is_sent_but_not_asked_by_input(self):
        out = self.run_configure(["LAB", "", "", "n", "n"])
        self.assertEqual(out, b"BEGIN\nCFG ssid=LAB\nCFG password=secret\nAPPLY\n")


class MainArgumentTests(unittest.TestCase):
    def test_invalid_set_value_stops_before_opening_port(self):
        with mock.patch.object(sys, "argv", ["usb_viewer.py", "--port", "COM9", "--set", "servo_pin=19"]), \
                mock.patch.object(v, "open_device") as opened, \
                mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            v.main()
        opened.assert_not_called()


try:
    import cv2
    import numpy as np
except ImportError:  # Viewer drawing needs .venv-camera (opencv + numpy).
    cv2 = None


@unittest.skipIf(cv2 is None, "OpenCV not installed")
class RenderTests(unittest.TestCase):
    def run_simulated_viewer(self, args, events):
        from types import SimpleNamespace
        from itertools import count
        serial = SimpleNamespace(in_waiting=65536, read=mock.Mock(side_effect=events),
                                 write=mock.Mock(), flush=mock.Mock())
        opened = mock.MagicMock()
        opened.__enter__.return_value = serial
        texts = []
        fake_cv2 = SimpleNamespace(
            IMREAD_COLOR=1, FONT_HERSHEY_SIMPLEX=0,
            imdecode=mock.Mock(return_value=np.zeros((240, 320, 3), np.uint8)),
            putText=lambda screen, text, *rest: texts.append(text),
            imshow=mock.Mock(),
            waitKey=mock.Mock(side_effect=[-1] * (len(events) - 1) + [ord('q')]),
            destroyAllWindows=mock.Mock())
        ticks = count(100)
        with mock.patch.object(sys, "argv", ["usb_viewer.py", "--port", "TEST", *args]), \
                mock.patch.object(v, "open_device", return_value=opened), \
                mock.patch.dict(sys.modules, {"cv2": fake_cv2}), \
                mock.patch.object(v.time, "monotonic", side_effect=lambda: next(ticks)), \
                mock.patch("builtins.print"):
            result = v.main()
        return result, serial, fake_cv2, texts, opened

    def test_preview_heartbeat_q_cleanup_and_no_config_replay(self):
        from usb_protocol import packet
        events = [packet({"kind": "status", "phase": "CAMERA_TEST", "preview": True})] * 10
        result, serial, cv, _, opened = self.run_simulated_viewer(["--preview"], events)
        self.assertIsNone(result)
        writes = [call.args[0] for call in serial.write.call_args_list]
        self.assertGreaterEqual(writes.count(b"PREVIEW ON\n"), 2)
        self.assertEqual(writes[-1], b"PREVIEW OFF\n")
        self.assertFalse(any(b"BEGIN" in write for write in writes))
        cv.destroyAllWindows.assert_called_once()
        opened.__exit__.assert_called_once()

    def test_usb_disconnect_closes_then_fresh_run_does_not_resend_config(self):
        from usb_protocol import packet
        events = [packet({"kind": "status", "preview": True}), OSError("USB unplugged")]
        result, serial, cv, _, opened = self.run_simulated_viewer(
            ["--preview", "--set", "hold_sec=12"], events)
        self.assertEqual(result, 1)
        writes = [call.args[0] for call in serial.write.call_args_list]
        self.assertEqual(sum(b"BEGIN" in write for write in writes), 1)
        cv.destroyAllWindows.assert_called_once()
        opened.__exit__.assert_called_once()
        result, serial, cv, _, _ = self.run_simulated_viewer(["--preview"], [packet({"kind": "boot"})])
        self.assertIsNone(result)
        self.assertFalse(any(b"BEGIN" in call.args[0] for call in serial.write.call_args_list))

    def test_rejected_config_is_visible_and_not_retried(self):
        from usb_protocol import packet
        events = [packet({"kind": "config", "ok": False}),
                  packet({"kind": "status", "phase": "STANDBY"})]
        result, serial, _, texts, _ = self.run_simulated_viewer(["--set", "hold_sec=12"], events)
        self.assertIsNone(result)
        self.assertTrue(any("Configuration rejected" in text for text in texts))
        self.assertEqual(sum("Config rejected - check" in text for text in texts), 2)
        serial.write.assert_called_once_with(b"BEGIN\nCFG hold_sec=12\nAPPLY\n")

    def test_viewer_clears_previous_person_and_rejects_late_images(self):
        # Exercise the actual loop without a window or serial hardware.
        from types import SimpleNamespace
        from usb_protocol import packet
        messages = [
            ({"kind": "frame", "session_id": 7}, b"jpg"),
            ({"kind": "scale", "session_id": 8, "phase": "ACTIVE"}, b""),
            ({"kind": "frame", "session_id": 7}, b"late"),
            ({"kind": "frame", "session_id": 8}, b"jpg"),
            ({"kind": "scale", "session_id": 8, "phase": "DRAINING"}, b""),
            ({"kind": "frame", "session_id": 8}, b"late"),
            ({"kind": "scale", "session_id": 8, "phase": "RESULT_HOLD",
              "height_cm": 180, "weight_kg": 81, "bmi": 25}, b""),
            ({"kind": "boot"}, b""),
        ]
        serial = SimpleNamespace(in_waiting=65536,
                                 read=mock.Mock(side_effect=[packet(m, j) for m, j in messages]))
        opened = mock.MagicMock()
        opened.__enter__.return_value = serial
        screens, texts = [], []
        fake_cv2 = SimpleNamespace(
            IMREAD_COLOR=1, FONT_HERSHEY_SIMPLEX=0,
            imdecode=mock.Mock(return_value=np.zeros((240, 320, 3), np.uint8)),
            putText=lambda screen, text, *args: texts.append(text),
            imshow=lambda title, screen: screens.append(screen.copy()),
            waitKey=mock.Mock(side_effect=[-1] * (len(messages) - 1) + [ord('q')]),
            destroyAllWindows=mock.Mock())
        with mock.patch.object(sys, "argv", ["usb_viewer.py", "--port", "TEST"]), \
                mock.patch.object(v, "open_device", return_value=opened), \
                mock.patch.dict(sys.modules, {"cv2": fake_cv2}), \
                mock.patch.object(v, "render_frame", side_effect=[
                    np.full((480, 800, 3), 11, np.uint8),
                    np.full((480, 800, 3), 22, np.uint8)]) as render, \
                mock.patch("builtins.print"):
            v.main()
        self.assertEqual(render.call_count, 2)
        self.assertTrue((screens[0] == 11).all())
        self.assertTrue((screens[3] == 22).all())
        for index in (1, 2, 4, 5, 6, 7):
            self.assertFalse(screens[index].any())
        self.assertIn("Height: 180.0 cm | BMI: 25.0", texts)
        fake_cv2.destroyAllWindows.assert_called_once()

    def test_render_new_old_and_broken_frames(self):
        for meta in (NEW_FRAME, OLD_FRAME, {}, {"boxes": None, "margin_px": "x"},
                     dict(NEW_FRAME, boxes=[[1, 2, 3]], framing="TopClipped", margin_px=500)):
            with self.subTest(meta=meta):
                canvas = v.render_frame(cv2, np.zeros((240, 320, 3), np.uint8), meta)
                self.assertEqual(canvas.shape[1], 800)
                self.assertEqual(canvas.shape[0], 600)


if __name__ == "__main__":
    unittest.main()
