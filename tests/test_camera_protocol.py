from pathlib import Path
import sys
import unittest

import requests

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "camera"))
from protocol import pedestrian_label, serial_control_url, ultralytics_model_path


class CameraProtocolTests(unittest.TestCase):
    def test_requests_keeps_firmware_delimiters_literal(self):
        for label, confidence in (("MEAS_H170.0_D2.500", 0.95), ("NO_PEOPLE", 0),
                                  ("PERSON_UNCALIBRATED", 0.8), ("PERSON_PARTIAL", 0.9)):
            with self.subTest(label=label):
                url = serial_control_url("http://10.127.7.68/", label, confidence)
                prepared = requests.Request("GET", url).prepare()
                self.assertNotIn("%3B", prepared.url)
                command = prepared.url.split("serial=", 1)[1].split(";")
                self.assertEqual(command, [label, f"{confidence:.3f}", "stop"])

    def test_old_params_encoding_is_incompatible_with_literal_split(self):
        url = requests.Request("GET", "http://camera/control",
                               params={"serial": "MEAS_H170.0_D2.500;0.950;stop"}).prepare().url
        self.assertIn("%3B", url)
        self.assertEqual(len(url.split("serial=", 1)[1].split(";")), 1)

    def test_rejects_injected_delimiters_and_invalid_confidence(self):
        for label in ("PERSON;stop", "PERSON&restart=1", "No Face", ""):
            with self.assertRaises(ValueError):
                serial_control_url("http://camera", label, 0.9)
        for confidence in (float("nan"), float("inf"), -0.1, 1.1):
            with self.assertRaises(ValueError):
                serial_control_url("http://camera", "NO_PEOPLE", confidence)


    def test_pedestrian_label_matches_firmware(self):
        # Same grammar as decodePedestrianLabel() in lib/Measurements/pedestrian_state.h.
        import re
        firmware = re.compile(r"PED_(WARN|SAFE)_D(\d+\.\d+)")
        for distance, expect in ((1.8, "PED_WARN_D1.80"), (2.5, "PED_SAFE_D2.50"),
                                 (3.204, "PED_SAFE_D3.20"), (120.0, "PED_SAFE_D50.00"),
                                 (0.001, "PED_WARN_D0.01")):
            with self.subTest(distance=distance):
                label = pedestrian_label(distance, 2.5)
                self.assertEqual(label, expect)
                m = firmware.fullmatch(label)
                self.assertTrue(m)
                self.assertTrue(0 < float(m.group(2)) <= 50)
                url = requests.Request("GET", serial_control_url("http://cam", label, 0.9)).prepare().url
                self.assertEqual(url.split("serial=", 1)[1].split(";"), [label, "0.900", "stop"])
        for bad in (0, -1, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                pedestrian_label(bad, 2.5)


    def test_ultralytics_model_path_avoids_apostrophe_and_download(self):
        import os
        root = Path(__file__).resolve().parents[1]
        old = os.getcwd()
        try:
            os.chdir(root)  # "Mira's Laptop" is above the repo: must not appear in the path.
            self.assertEqual(ultralytics_model_path(root / "yolo26n.pt"), "yolo26n.pt")
            self.assertNotIn("'", ultralytics_model_path(root / "yolo26n.pt"))
            with self.assertRaises(FileNotFoundError):  # Missing file: error, never a download.
                ultralytics_model_path(root / "no_such_model.pt")
        finally:
            os.chdir(old)


if __name__ == "__main__":
    unittest.main()
