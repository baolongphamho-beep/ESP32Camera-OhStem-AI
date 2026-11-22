"""TV5: every fixed LCD text in the native firmware fits the 16x2 LCD (16 characters per line).

Scans firmware/edge_yolo/main/*.cpp for lcdMessage("..", "..") and lcdSetLine(n, "..") string
literals. A 17th character is silently cut on the real LCD (e.g. "Model input error").
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware" / "edge_yolo" / "main"
LITERAL = r'"((?:[^"\\]|\\.)*)"'
PATTERNS = [re.compile(r"lcdMessage\(\s*" + LITERAL + r"(?:\s*,\s*" + LITERAL + r")?"),
            re.compile(r"lcdSetLine\(\s*\d+\s*,\s*" + LITERAL)]


def lcd_literals():
    found = []
    for path in sorted(MAIN.glob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        for pattern in PATTERNS:
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                for literal in match.groups():
                    if literal is not None and "%" not in literal:
                        found.append((path.name, line, literal))
    return found


class LcdTextLengthTests(unittest.TestCase):
    def test_scanner_finds_the_lcd_texts(self):
        texts = {t for _, _, t in lcd_literals()}
        self.assertIn("Step on scale", texts)
        self.assertGreater(len(texts), 10)

    def test_every_fixed_lcd_text_fits_16_columns(self):
        too_long = [f"{name}:{line} '{text}' ({len(text)})" for name, line, text in lcd_literals() if len(text) > 16]
        self.assertEqual(too_long, [], "LCD texts longer than 16 characters")


if __name__ == "__main__":
    unittest.main()
