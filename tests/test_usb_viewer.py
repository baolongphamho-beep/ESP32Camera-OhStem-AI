import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "camera"))
from usb_viewer import format_metric, format_timing_line, validate_mac


class USBViewerHelperTests(unittest.TestCase):
    def test_validate_mac_accepts_empty_and_normalizes_hex(self):
        self.assertEqual(validate_mac(""), "")
        self.assertEqual(validate_mac("   "), "")
        self.assertEqual(validate_mac(" 70:87:9E:EA:3A:E0 "), "70:87:9e:ea:3a:e0")
        self.assertEqual(validate_mac("00:00:00:00:00:00"), "00:00:00:00:00:00")

    def test_validate_mac_rejects_malformed_addresses(self):
        for bad in ("70-87-9e-ea-3a-e0", "70:87:9e:ea:3a", "zz:87:9e:ea:3a:e0", "70:87:9e:ea:3a:e0:11"):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    validate_mac(bad)

    def test_format_metric_handles_missing_and_valid_values(self):
        self.assertEqual(format_metric(None, "cm", ".1f"), "--")
        self.assertEqual(format_metric(float("nan"), "kg", ".2f"), "--")
        self.assertEqual(format_metric(float("inf"), "m", ".2f"), "--")
        self.assertEqual(format_metric(True, "kg", ".2f"), "--")
        self.assertEqual(format_metric(170.34, "cm", ".1f"), "170.3 cm")
        self.assertEqual(format_metric(65.5, "kg", ".2f"), "65.50 kg")
        self.assertEqual(format_metric(22.456, "", ".2f"), "22.46")

    def test_format_timing_line_handles_keep_alive_rssi_and_fallbacks(self):
        self.assertEqual(
            format_timing_line({
                "processing_ms": 518,
                "capture_ms": 120,
                "keep_alive": True,
                "wifi_rssi": -52,
            }),
            "AI: 518 ms   Net: 120 ms [KA]   RSSI: -52 dBm",
        )
        self.assertEqual(
            format_timing_line({
                "processing_ms": 480,
                "capture_ms": 210,
                "keep_alive": False,
                "wifi_rssi": 0,
            }),
            "AI: 480 ms   Net: 210 ms [NEW]",
        )
        self.assertEqual(
            format_timing_line({"processing_ms": 500, "capture_ms": 95}),
            "AI: 500 ms   Net: 95 ms",
        )
        self.assertEqual(
            format_timing_line({"processing_ms": 450, "wifi_rssi": True}),
            "On-board processing: 450 ms",
        )


if __name__ == "__main__":
    unittest.main()
