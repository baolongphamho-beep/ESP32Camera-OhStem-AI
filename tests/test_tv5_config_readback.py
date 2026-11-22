"""TV5: USB "CONFIG?" read-back on the PC side (usb_protocol.format_config, usb_viewer --show-config)."""
import pathlib
import sys
import unittest
from itertools import count
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "camera"))
import usb_viewer as v  # noqa: E402
from usb_protocol import CONFIG_QUERY, config_set_args, format_config, packet  # noqa: E402

# Shape of firmware/edge_yolo/main/config_readback.h output.
VALUES = {"kind": "config_values", "ssid": "LAB", "password_set": True, "camera": "http://10.127.7.68/capture",
          "mac": "70:87:9e:ea:3a:e0", "mode": "ground", "distance": 3.0, "reference": 183.0, "focal": 1.33,
          "height": 110.0, "tilt": 0.0, "hold_sec": 10, "servo_pin": 10, "servo_min": 45.0, "servo_max": 135.0,
          "servo_bottom": 112.0, "servo_top": 85.0, "servo_step": 4.5, "servo_level": 92.5, "servo_dir": 1,
          "servo_settle": 200, "revision": 3}


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
    def test_format_lists_keys_and_meaning_without_password(self):
        text = "\n".join(format_config(VALUES))
        for part in ("servo_level = 92.5", "servo_bottom = 112", "servo_top = 85", "hold_sec = 10",
                     "servo_dir = 1 (tang goc -> camera CUI XUONG)", "password_set = co", "mode = ground"):
            self.assertIn(part, text)
        self.assertNotIn("password =", text)
        self.assertIn("NGANG LEN", "\n".join(format_config(dict(VALUES, servo_dir=-1))))
        self.assertEqual(format_config({"kind": "status"}), ["Khong phai goi cau hinh."])
        self.assertIn("loi", format_config({"kind": "config_values", "error": "too long"})[0])

    def test_backup_text_reapplies_calibration(self):
        self.assertEqual(config_set_args(VALUES),
                         "servo_level=92.5 servo_dir=1 servo_bottom=112 servo_top=85 servo_min=45 "
                         "servo_max=135 height=110 focal=1.33")

    def test_read_config_skips_other_packets(self):
        port = FakePort([packet({"kind": "status"}), packet(VALUES)])
        self.assertEqual(v.read_config(port)["servo_level"], 92.5)
        self.assertEqual(port.written, [CONFIG_QUERY])
        ticks = count(0, 1.0)
        self.assertIsNone(v.read_config(FakePort([]), timeout=3, clock=lambda: next(ticks)))

    def test_show_config_cli_without_window(self):
        port = FakePort([packet(VALUES)])
        with mock.patch.object(sys, "argv", ["usb_viewer.py", "--port", "TEST", "--show-config"]), \
                mock.patch.object(v, "open_device", return_value=port), \
                mock.patch.dict(sys.modules, {"cv2": None}), \
                mock.patch("builtins.print") as printed:
            self.assertEqual(v.main(), 0)
        out = "\n".join(str(c.args[0]) for c in printed.call_args_list if c.args)
        self.assertIn("servo_level = 92.5", out)
        self.assertIn("servo_level=92.5", out)

    def test_firmware_handles_config_query(self):
        src = (ROOT / "firmware/edge_yolo/main/edge_io.cpp").read_text(encoding="utf-8")
        self.assertIn('line == "CONFIG?"', src)
        self.assertIn("config_readback::toJson", src)


if __name__ == "__main__":
    unittest.main()
