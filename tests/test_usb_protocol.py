import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "camera"))
from usb_protocol import HEADER, Parser, SessionFilter, config_commands, packet


class USBProtocolTests(unittest.TestCase):
    def test_session_filter_rejects_departed_and_previous_person_frames(self):
        sessions = SessionFilter()
        self.assertTrue(sessions.accept({"kind": "frame"}))  # Legacy firmware.
        self.assertTrue(sessions.accept({"kind": "scale", "session_id": 1, "phase": "ACTIVE"}))
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 1}))
        self.assertTrue(sessions.accept({"kind": "scale", "session_id": 2, "phase": "DRAINING"}))
        self.assertFalse(sessions.accept({"kind": "frame", "session_id": 1}))
        self.assertFalse(sessions.accept({"kind": "frame", "session_id": 2}))
        self.assertTrue(sessions.accept({"kind": "scale", "session_id": 2, "phase": "RESULT_HOLD"}))
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 3}))
        self.assertFalse(sessions.accept({"kind": "scale", "session_id": 2, "phase": "STANDBY"}))

    def test_session_filter_accepts_phase3_scan_phases_and_rejects_sleep_phases(self):
        sessions = SessionFilter()
        for active_phase, sid in (("WAKING_HOMING", 10), ("SERVO_SWEEP", 11), ("LOCKED_MEASURE", 12)):
            self.assertTrue(sessions.accept({"kind": "scale", "session_id": sid, "phase": active_phase}))
            self.assertTrue(sessions.accept({"kind": "frame", "session_id": sid}))
        for sleep_phase, sid in (("DRAINING", 13), ("RESULT_HOLD", 14), ("STANDBY", 15)):
            self.assertTrue(sessions.accept({"kind": "scale", "session_id": sid, "phase": sleep_phase}))
            self.assertFalse(sessions.accept({"kind": "frame", "session_id": sid}))

    def test_session_filter_handles_generation_rollover(self):
        sessions = SessionFilter()
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 0xffffffff}))
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 0}))
        self.assertFalse(sessions.accept({"kind": "frame", "session_id": 0xffffffff}))
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 40}))
        self.assertTrue(sessions.accept({"kind": "boot"}))
        self.assertTrue(sessions.accept({"kind": "scale", "session_id": 0, "phase": "STANDBY"}))

    def test_split_reads_and_noise(self):
        stream = b"boot noise" + packet({"kind": "frame", "height_cm": 183}, b"jpeg")
        parser = Parser()
        result = []
        for byte in stream:
            result += parser.feed(bytes([byte]))
        self.assertEqual(result, [({"kind": "frame", "height_cm": 183}, b"jpeg")])

    def test_crc_failure_and_invalid_lengths_resynchronize(self):
        bad = bytearray(packet({"kind": "frame"}, b"jpeg"))
        bad[-1] ^= 1
        invalid = HEADER.pack(b"SSV1", 999999, 0, 0)
        result = Parser().feed(invalid + bad + packet({"kind": "status"}))
        self.assertEqual(result, [({"kind": "status"}, b"")])

    def test_truncated_large_frame_does_not_block_following_packet(self):
        old = packet({"kind": "frame"}, b"x" * 20000)[:100]
        self.assertEqual(Parser().feed(old + packet({"kind": "config", "ok": True})),
                         [({"kind": "config", "ok": True}, b"")])

    def test_configuration_rejects_line_injection_and_byte_overflow(self):
        for value in ("abc\nAPPLY", "\x00", "é" * 17):
            with self.assertRaises(ValueError):
                config_commands({"ssid": value})
        with self.assertRaises(ValueError):
            config_commands({"hold_sec": "1000"})
        with self.assertRaises(ValueError):
            config_commands({"hold_sec": "10\nAPPLY"})
        self.assertEqual(config_commands({"mode": "fixed", "focal": 0, "hold_sec": 10}),
                         b"BEGIN\nCFG mode=fixed\nCFG focal=0\nCFG hold_sec=10\nAPPLY\n")

    def test_configuration_supports_servo_keys_and_validates_bounds(self):
        payload = config_commands({
            "servo_pin": 10,
            "servo_min": 40,
            "servo_max": 140,
            "servo_bottom": 68,
            "servo_top": 95,
            "servo_step": 4.5,
            "servo_level": 90,
            "servo_dir": -1,
            "servo_settle": 200,
        })
        self.assertEqual(
            payload,
            b"BEGIN\nCFG servo_pin=10\nCFG servo_min=40\nCFG servo_max=140\n"
            b"CFG servo_bottom=68\nCFG servo_top=95\nCFG servo_step=4.5\n"
            b"CFG servo_level=90\nCFG servo_dir=-1\nCFG servo_settle=200\nAPPLY\n",
        )
        with self.assertRaises(ValueError):
            config_commands({"servo_pin": "10000"})
        with self.assertRaises(ValueError):
            config_commands({"servo_dir": "-10"})
        with self.assertRaises(ValueError):
            config_commands({"servo_settle": "20000"})
        with self.assertRaises(ValueError):
            config_commands({"servo_level": "90\nAPPLY"})


if __name__ == "__main__":
    unittest.main()
