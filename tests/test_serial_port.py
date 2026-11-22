import pathlib
import sys
import unittest
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "camera"))
import serial_port as sp  # noqa: E402


def port(device, vid=None, pid=None, description="USB device"):
    return SimpleNamespace(device=device, vid=vid, pid=pid, description=description)


UNO = port("COM7", 0x303A, 0x1001, "USB JTAG/serial debug unit")
CH340 = port("COM3", 0x1A86, 0x7523, "USB-SERIAL CH340")
BT = port("COM4", None, None, "Standard Serial over Bluetooth link")


class SerialPortTests(unittest.TestCase):
    def test_explicit_port_is_used_unchanged(self):
        self.assertEqual(sp.resolve_port("COM5", ports=[]), "COM5")
        self.assertEqual(sp.resolve_port(" COM12 ", ports=[UNO]), "COM12")

    def test_auto_picks_the_single_espressif_port_whatever_its_number(self):
        for number in ("COM5", "COM7", "COM11"):
            uno = port(number, 0x303A, 0x1001)
            self.assertEqual(sp.resolve_port("auto", ports=[BT, uno, CH340]), number)
        self.assertEqual(sp.resolve_port(None, ports=[UNO]), "COM7")
        self.assertEqual(sp.resolve_port("AUTO", ports=[UNO]), "COM7")

    def test_auto_never_guesses(self):
        with self.assertRaises(sp.PortError) as none:
            sp.resolve_port("auto", ports=[BT, CH340])
        self.assertIn("COM3", str(none.exception))
        self.assertIn("--port COMx", str(none.exception))
        with self.assertRaises(sp.PortError):
            sp.resolve_port("auto", ports=[])
        with self.assertRaises(sp.PortError) as many:
            sp.resolve_port("auto", ports=[UNO, port("COM9", 0x303A, 0x1001)])
        self.assertIn("COM9", str(many.exception))

    def test_port_or_exit_prints_reason_and_exits_2(self):
        with mock.patch.object(sp, "_ports", return_value=[]), mock.patch("sys.stderr"), \
                self.assertRaises(SystemExit) as stop:
            sp.port_or_exit("auto")
        self.assertEqual(stop.exception.code, 2)
        with mock.patch.object(sp, "_ports", return_value=[UNO]), mock.patch("builtins.print"):
            self.assertEqual(sp.port_or_exit("auto"), "COM7")

    def test_tools_default_to_auto(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        for rel in ("camera/usb_viewer.py", "camera/power_monitor.py", "camera/servo_bench.py"):
            text = (root / rel).read_text(encoding="utf-8")
            with self.subTest(rel=rel):
                self.assertIn("add_port_argument", text)
                self.assertIn("port_or_exit", text)
                self.assertNotIn('"--port", required=True', text)


if __name__ == "__main__":
    unittest.main()
