import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "camera"))
from geometry import calibrate_focal, fixed_distance_height, ground_plane_height, measurement_label
from smartscale_camera import resolve_focal, configure


class GeometryTests(unittest.TestCase):
    def test_mounting_setup_can_be_changed_without_reflashing(self):
        with patch("builtins.input", side_effect=["ground", "-", "130", "15"]):
            settings = configure({"mode": "fixed", "focal_px": 600})
        self.assertEqual(settings["mode"], "ground")
        self.assertIsNone(settings["focal_px"])
        self.assertEqual(settings["camera_height_cm"], 130)
        self.assertEqual(settings["tilt_deg"], 15)

    def test_reference_then_different_person(self):
        focal = calibrate_focal(340, 3, 170)
        self.assertAlmostEqual(focal, 600)
        height, distance = fixed_distance_height(360, focal, 3)
        self.assertAlmostEqual(height, 180)
        self.assertEqual(distance, 3)

    def test_auto_calibrates_first_frame_without_wait(self):
        settings = {"mode": "fixed", "distance_m": 3, "reference_cm": 170}
        focal, saved = resolve_focal(settings, {}, (640, 480), 340)
        self.assertEqual(focal, 600)
        self.assertEqual(saved["schema"], 2)
        focal2, saved2 = resolve_focal(settings, saved, (640, 480), 360)
        self.assertIsNone(saved2)
        self.assertEqual(fixed_distance_height(360, focal2, 3)[0], 180)

    def test_focal_resolution_mismatch_without_reference_is_rejected(self):
        with self.assertRaises(ValueError):
            resolve_focal({"mode": "ground"}, {"schema": 2, "focal_px": 600,
                          "resolution": [640, 480]}, (320, 240), 100)
        # A reference value saved from an earlier person must not silently
        # recalibrate a new person when the capture resolution changes.
        with self.assertRaises(ValueError):
            resolve_focal({"mode": "fixed", "distance_m": 3, "reference_cm": 170},
                          {"schema": 2, "focal_px": 600, "resolution": [640, 480]}, (320, 240), 100)

    def test_ground_geometry_recovers_independent_height_and_distance(self):
        # Forward-project known points, then reconstruct at several camera pitches.
        for pitch in (-5, 0, 15, 25):
            with self.subTest(pitch=pitch):
                h_cam, person_h, z, focal = 1.2, 1.8, 4.0, 300
                theta = math.radians(pitch)
                def project(world_height):
                    up = world_height - h_cam
                    camera_up = up * math.cos(theta) + z * math.sin(theta)
                    camera_z = -up * math.sin(theta) + z * math.cos(theta)
                    return 239.5 - focal * camera_up / camera_z
                box = (300, project(person_h), 339, project(0))
                measured_h, measured_d = ground_plane_height(box, (640, 480), focal, 120, pitch)
                self.assertAlmostEqual(measured_h, 180, places=5)
                self.assertAlmostEqual(measured_d, 4, places=5)

    def test_ray_above_horizon_is_invalid(self):
        with self.assertRaises(ValueError):
            ground_plane_height((280, 100, 360, 200), (640, 480), 600, 120)

    def test_missing_physical_information_does_not_invent_height(self):
        with self.assertRaises(ValueError):
            resolve_focal({"mode": "fixed"}, {}, (640, 480), 300)
        with self.assertRaises(ValueError):
            fixed_distance_height(300, 600, None)

    def test_firmware_label_has_no_protocol_delimiters(self):
        label = measurement_label(170, 2.5)
        self.assertEqual(label, "MEAS_H170.0_D2.500")
        self.assertNotIn(",", label)
        self.assertNotIn(";", label)


if __name__ == "__main__":
    unittest.main()
