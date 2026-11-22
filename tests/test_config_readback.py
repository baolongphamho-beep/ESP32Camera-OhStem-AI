"""CONFIG? read-back on the PC side: formatting, backup command, viewer and jog tool."""
import pathlib
import sys
import unittest
from itertools import count
from types import SimpleNamespace
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "camera"))
sys.path.insert(0, str(ROOT / "tools"))
import servo_jog  # noqa: E402
import usb_viewer as v  # noqa: E402
from usb_protocol import CONFIG_QUERY, config_set_args, format_config, packet  # noqa: E402

VALUES = {"kind": "config_values", "servo_pin": 10, "servo_bottom": 130.0, "servo_top": 60.0,
          "servo_step": 5.0, "servo_level": 92.5, "servo_dir": 1, "servo_settle": 200,
          "servo_delay": 500, "servo_dps": 4.0, "hold_sec": 12.0, "height": 110.0, "focal": 1.33,
          "mac": "70:87:9E:EA:3A:E0", "camera": "http://10.127.7.68/capture", "cam_framesize": -1,
          "cam_quality": 12, "ssid": "LAB", "password_set": True, "servo_limits": [45.0, 135.0]}


class FakePort:
    def __init__(self, replies):
        self.replies = list(replies)
        self.written = []
        self.in_waiting = 0

    def write(self, data):
        self.written.append(data)

    def read(self, _n):
        return self.replies.pop(0) if self.replies else b""

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class ConfigReadbackTests(unittest.TestCase):
    def test_format_shows_meaning_and_never_a_password(self):
        text = "\n".join(format_config(VALUES))
        self.assertIn("servo_level = 92.5", text)
        self.assertIn("servo_dir = 1 (tang goc -> camera CUI XUONG)", text)
        self.assertIn("servo_bottom = 130", text)
        self.assertIn("password_set = co", text)
        self.assertIn("45..135", text)
        self.assertNotIn("password =", text)
        self.assertIn("NGANG LEN", "\n".join(format_config(dict(VALUES, servo_dir=-1))))
        self.assertEqual(format_config({"kind": "status"}), ["Khong phai goi cau hinh."])
        self.assertIn("loi", format_config({"kind": "config_values", "error": "too long"})[0])

    def test_backup_command_reapplies_calibration(self):
        self.assertEqual(config_set_args(VALUES),
                         "--set servo_level=92.5 --set servo_dir=1 --set servo_bottom=130 --set servo_top=60")

    def test_read_config_sends_query_and_skips_other_packets(self):
        port = FakePort([packet({"kind": "status", "phase": "STANDBY"}), packet(VALUES)])
        self.assertEqual(v.read_config(port)["servo_level"], 92.5)
        self.assertEqual(port.written, [CONFIG_QUERY])
        ticks = count(0, 1.0)
        self.assertIsNone(v.read_config(FakePort([]), timeout=3, clock=lambda: next(ticks)))

    def test_show_config_cli_prints_values_without_gui(self):
        port = FakePort([packet(VALUES)])
        with mock.patch.object(sys, "argv", ["usb_viewer.py", "--port", "TEST", "--show-config"]), \
                mock.patch.object(v, "open_device", return_value=port), \
                mock.patch.dict(sys.modules, {"cv2": None}), \
                mock.patch("builtins.print") as printed:
            self.assertEqual(v.main(), 0)
        out = "\n".join(str(c.args[0]) for c in printed.call_args_list if c.args)
        self.assertIn("servo_level = 92.5", out)
        self.assertIn("--set servo_level=92.5", out)

    def test_show_config_reports_old_firmware(self):
        ticks = count(0, 1.0)
        with mock.patch.object(v, "open_device", return_value=FakePort([])), \
                mock.patch.object(v.time, "monotonic", side_effect=lambda: next(ticks)), \
                mock.patch("builtins.print") as printed:
            self.assertEqual(v.show_config("TEST"), 1)
        self.assertTrue(any("CONFIG?" in str(c) for c in printed.call_args_list))

    def test_jog_config_command_queries_the_board(self):
        data, msg, running = servo_jog.JogSession().handle("CONFIG")
        self.assertEqual(data, CONFIG_QUERY)
        self.assertTrue(running)


if __name__ == "__main__":
    unittest.main()
