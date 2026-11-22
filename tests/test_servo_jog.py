import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import servo_jog  # noqa: E402


class JogSessionTests(unittest.TestCase):
    def test_moves_are_clamped_and_sent_as_servo_lines(self):
        s = servo_jog.JogSession(start=90)
        self.assertEqual(s.servo_line(), b"SERVO 90\n")
        self.assertEqual(s.handle("92.5")[0], b"SERVO 92.5\n")
        self.assertEqual(s.handle("+")[0], b"SERVO 93.5\n")
        self.assertEqual(s.handle("-5")[0], b"SERVO 88.5\n")
        s.handle("133"); self.assertEqual(s.handle("+5")[0], b"SERVO 135\n")  # hard limit 45..135
        for outside in ("200", "136", "44", "0"):
            data, msg, running = s.handle(outside)
            self.assertIsNone(data); self.assertIn("45..135", msg); self.assertTrue(running)
        self.assertEqual(s.deg, 135)
        s.handle("47"); self.assertEqual(s.handle("-5")[0], b"SERVO 45\n")

    def test_marks_and_save_use_validated_config(self):
        s = servo_jog.JogSession(start=88)
        s.handle("level"); s.handle("45"); s.handle("bottom"); s.handle("130"); s.handle("top"); s.handle("up")
        self.assertEqual(s.marks, {"servo_level": 88, "servo_bottom": 45, "servo_top": 130, "servo_dir": -1})
        data, msg, _ = s.handle("save")
        self.assertEqual(data, b"BEGIN\nCFG servo_level=88\nCFG servo_bottom=45\nCFG servo_top=130\n"
                               b"CFG servo_dir=-1\nAPPLY\n")
        s.handle("down"); self.assertEqual(s.marks["servo_dir"], 1)

    def test_save_refuses_bottom_equal_top_and_empty(self):
        s = servo_jog.JogSession(start=90)
        self.assertIsNone(s.handle("save")[0])
        s.handle("bottom"); s.handle("top")
        data, msg, _ = s.handle("save")
        self.assertIsNone(data); self.assertIn("differ", msg)

    def test_quit_releases_and_unknown_is_harmless(self):
        s = servo_jog.JogSession()
        self.assertEqual(s.handle("q"), (b"SERVO OFF\n", "Nha servo.", False))
        data, msg, running = s.handle("xyz")
        self.assertIsNone(data); self.assertTrue(running)
        self.assertEqual(s.handle("   ")[0], None)
        self.assertEqual(servo_jog.JogSession(start=500).deg, 135)
        self.assertEqual(servo_jog.JogSession(start=0).deg, 45)


if __name__ == "__main__":
    unittest.main()
