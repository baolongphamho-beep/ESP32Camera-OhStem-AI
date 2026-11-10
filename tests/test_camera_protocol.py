from pathlib import Path
import sys
import unittest

import requests

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "camera"))
from protocol import serial_control_url


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


if __name__ == "__main__":
    unittest.main()
