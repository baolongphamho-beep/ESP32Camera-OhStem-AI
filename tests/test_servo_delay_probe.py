import math
import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import servo_delay_probe as probe  # noqa: E402


class ServoDelayProbeTests(unittest.TestCase):
    def test_travel_time_matches_firmware_profile(self):
        # Same numbers as tests/servo_motion_test.cpp (servoTravelMs).
        self.assertEqual(probe.travel_ms(0), 0)
        self.assertTrue(289 <= probe.travel_ms(5) <= 330)
        self.assertTrue(1750 <= probe.travel_ms(90) <= 1790)
        self.assertEqual(probe.travel_ms(-10), probe.travel_ms(10))
        src = (pathlib.Path(__file__).resolve().parents[1] / "lib/EdgeSweep/servo_motion.h").read_text(encoding="utf-8")
        self.assertEqual(float(re.search(r"SWEEP_SERVO_MOVE_DPS ([\d.]+)f", src).group(1)), probe.MOVE_DPS)
        self.assertEqual(float(re.search(r"SWEEP_SERVO_ACCEL_DPS2 ([\d.]+)f", src).group(1)), probe.ACCEL_DPS2)

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
