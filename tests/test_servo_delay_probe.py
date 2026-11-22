import math
import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import servo_delay_probe as probe  # noqa: E402


class ServoDelayProbeTests(unittest.TestCase):
    def test_travel_time_matches_firmware_ramp(self):
        # servoMoveDeg(): ceil(dist / 2 deg) steps of 20 ms (servo_math.h constants).
        self.assertEqual(probe.travel_ms(0), 0)
        self.assertEqual(probe.travel_ms(0.04), 0)
        self.assertEqual(probe.travel_ms(4.5), 60)
        self.assertEqual(probe.travel_ms(-10), 100)
        self.assertEqual(probe.travel_ms(90), 900)
        src = (pathlib.Path(__file__).resolve().parents[1] / "firmware/edge_yolo/main/servo_math.h").read_text(encoding="utf-8")
        self.assertEqual(float(re.search(r"kServoRampMaxStepDeg = ([\d.]+)f", src).group(1)), probe.RAMP_STEP_DEG)
        self.assertEqual(int(re.search(r"kServoRampStepDelayMs = (\d+)U", src).group(1)), probe.RAMP_STEP_MS)
        self.assertEqual(float(re.search(r"kServoDefaultMinDeg = ([\d.]+)f", src).group(1)), probe.SERVO_TRAVEL[0])
        self.assertEqual(float(re.search(r"kServoDefaultMaxDeg = ([\d.]+)f", src).group(1)), probe.SERVO_TRAVEL[1])

    def test_age_is_last_stale_request_after_stop(self):
        # Servo stops at 400 ms; requests started up to 900 ms still show the old view.
        samples = [(0, 20), (200, 20), (450, 18), (700, 15), (900, 9), (1150, 0.5), (1400, 0.3)]
        self.assertEqual(probe.estimate_age(samples, stop_ms=400, threshold=3), 500)

    def test_current_immediately_and_never_settled(self):
        self.assertEqual(probe.estimate_age([(0, 10), (300, 1), (600, 0)], stop_ms=400, threshold=3), 0)
        self.assertIsNone(probe.estimate_age([(0, 10), (300, 9), (600, 8)], stop_ms=100, threshold=3))
        self.assertIsNone(probe.estimate_age([], stop_ms=0, threshold=3))

    def test_recommendation_covers_worst_case_with_margin(self):
        self.assertEqual(probe.recommend([420, 510, None, 380]), 650)   # 510 + 100 -> 650
        self.assertEqual(probe.recommend([0, 0]), 100)
        self.assertEqual(probe.recommend([5000]), 3000)                # firmware maximum
        self.assertIsNone(probe.recommend([None, None]))

    def test_frame_diff(self):
        try:
            import numpy as np
        except ImportError:
            self.skipTest("numpy missing")
        a = np.zeros((60, 80), np.uint8)
        b = a.copy(); b[:, :40] = 100
        self.assertTrue(math.isclose(probe.frame_diff(a, b), 50.0))
        self.assertEqual(probe.frame_diff(a, a), 0.0)


if __name__ == "__main__":
    unittest.main()
