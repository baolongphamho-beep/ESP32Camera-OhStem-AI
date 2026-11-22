import math
import unittest
from app import height_m, parse_weight

class MeasurementTests(unittest.TestCase):
    def test_known_geometry(self):
        self.assertAlmostEqual(height_m(1, 2, math.degrees(math.atan(.35)), 600, 240, 240), 1.7)
        self.assertAlmostEqual(height_m(1, 2, 0, 600, 240, 30), 1.7)

    def test_invalid_geometry(self):
        for args in [(1, 0, 0, 600, 240, 20), (1, 2, 0, 0, 240, 20), (1, 2, float('nan'), 600, 240, 20)]:
            with self.assertRaises(ValueError):
                height_m(*args)

    def test_weight_protocol(self):
        self.assertEqual(parse_weight('{"type":"weight","weight_kg":60.5}'), 60.5)
        self.assertIsNone(parse_weight('{"type":"weight","weight_kg":-2}'))
        self.assertIsNone(parse_weight('{"type":"weight","weight_kg":NaN}'))
        self.assertIsNone(parse_weight('unrelated log 60.5'))
        line = '[MiScale2] Weight received: 100.0000'
        self.assertIsNone(parse_weight(line))
        self.assertAlmostEqual(parse_weight(line, 'lb'), 45.359237)
        self.assertEqual(parse_weight(line, 'kg'), 100)

if __name__ == '__main__':
    unittest.main()
