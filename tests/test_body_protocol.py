"""vision/body_protocol.py: labels must be exactly what lib/EdgeSweep/edge_message.h parses."""
from pathlib import Path
import re
import sys
import unittest

import requests

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "vision"))
sys.path.insert(0, str(ROOT / "camera"))
from body_protocol import Person, box_label, choose_person, frame_message  # noqa: E402
from protocol import serial_control_url  # noqa: E402

# Grammar of parseBodyMessage(): four unsigned numbers in 0..1, conf 0..100.
FIRMWARE_BOX = re.compile(r"BOX_(\d+\.\d+)_(\d+\.\d+)_(\d+\.\d+)_(\d+\.\d+)_(\d{1,3})")


class BodyProtocolTests(unittest.TestCase):
    def test_box_label_is_normalized_and_parseable(self):
        label = box_label(Person(128, 24, 192, 228, 0.876), 320, 240)
        self.assertEqual(label, "BOX_0.400_0.100_0.600_0.950_88")
        m = FIRMWARE_BOX.fullmatch(label)
        self.assertTrue(m)
        x1, y1, x2, y2, conf = (float(g) for g in m.groups())
        self.assertTrue(0 <= x1 < x2 <= 1 and 0 <= y1 < y2 <= 1 and 0 <= conf <= 100)
        url = requests.Request("GET", serial_control_url("http://cam", label, 0.88)).prepare().url
        self.assertEqual(url.split("serial=", 1)[1].split(";"), [label, "0.880", "stop"])

    def test_box_touching_or_outside_edges_is_clamped(self):
        # YOLO can return slightly negative / beyond-edge coordinates.
        self.assertEqual(box_label(Person(-3, -1, 330, 245, 1.2), 320, 240), "BOX_0.000_0.000_1.000_1.000_100")

    def test_invalid_boxes(self):
        for p in (Person(10, 10, 10, 50), Person(10, 50, 20, 40), Person(float("nan"), 0, 5, 5)):
            with self.assertRaises(ValueError):
                box_label(p, 320, 240)
        with self.assertRaises(ValueError):
            box_label(Person(1, 1, 5, 5), 0, 240)

    def test_choose_person_and_multi(self):
        big, small, similar = Person(0, 0, 100, 200), Person(0, 0, 20, 40), Person(150, 0, 240, 190)
        self.assertEqual(choose_person([]), (None, False))
        self.assertEqual(choose_person([small, big]), (big, False))      # far-away person ignored
        self.assertEqual(choose_person([similar, big]), (big, True))     # two people on/at the scale
        self.assertEqual(choose_person([small], min_area_px=1000), (None, False))

    def test_frame_message(self):
        self.assertEqual(frame_message([], 320, 240)[0], "NOBODY")
        self.assertEqual(frame_message([Person(0, 0, 100, 200), Person(150, 0, 240, 190)], 320, 240)[0], "MULTI")
        label, conf, person = frame_message([Person(128, 24, 192, 228, 0.9)], 320, 240)
        self.assertEqual(label, "BOX_0.400_0.100_0.600_0.950_90")
        self.assertAlmostEqual(conf, 0.9)
        self.assertIsNotNone(person)
        # A degenerate box never produces an invalid label.
        self.assertEqual(frame_message([Person(5, 5, 5, 50)], 320, 240)[0], "NOBODY")


if __name__ == "__main__":
    unittest.main()
