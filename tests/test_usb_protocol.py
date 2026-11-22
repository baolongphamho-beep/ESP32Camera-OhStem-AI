import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "camera"))
from usb_protocol import (FORBIDDEN_SERVO_PINS, HEADER, SERVO_LIMITS, Parser, SessionFilter,
                          config_commands, packet)


class USBProtocolTests(unittest.TestCase):
    def test_sessions_ignore_old_person_and_frames_after_departure(self):
        sessions = SessionFilter()
        self.assertTrue(sessions.accept({"kind": "frame"}))
        self.assertTrue(sessions.accept({"kind": "scale", "session_id": 1, "phase": "ACTIVE"}))
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 1}))
        self.assertTrue(sessions.accept({"kind": "scale", "session_id": 2, "phase": "DRAINING"}))
        self.assertFalse(sessions.accept({"kind": "frame", "session_id": 1}))
        self.assertFalse(sessions.accept({"kind": "frame", "session_id": 2}))
        self.assertTrue(sessions.accept({"kind": "scale", "session_id": 2, "phase": "RESULT_HOLD"}))
        self.assertFalse(sessions.accept({"kind": "frame", "session_id": 2}))
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 3}))
        self.assertFalse(sessions.accept({"kind": "scale", "session_id": 2, "phase": "STANDBY"}))

    def test_sessions_rollover_restart_and_preview(self):
        sessions = SessionFilter()
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 0xffffffff}))
        self.assertTrue(sessions.accept({"kind": "status", "session_id": 0, "phase": "STANDBY"}))
        self.assertFalse(sessions.accept({"kind": "frame", "session_id": 0xffffffff}))
        self.assertFalse(sessions.accept({"kind": "frame", "session_id": 0}))
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 0, "preview": True}))
        self.assertTrue(sessions.accept({"kind": "status", "session_id": 0, "phase": "SERVO_SWEEP"}))
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 0}))
        self.assertTrue(sessions.accept({"kind": "boot"}))
        self.assertTrue(sessions.accept({"kind": "frame", "session_id": 0xffffffff}))

    def test_malformed_session_ids_keep_legacy_compatibility(self):
        for session in (None, True, "3", [], {}):
            with self.subTest(session=session):
                self.assertTrue(SessionFilter().accept({"kind": "status", "session_id": session}))

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
        self.assertEqual(config_commands({"mode": "fixed", "focal": 0}),
                         b"BEGIN\nCFG mode=fixed\nCFG focal=0\nAPPLY\n")


class ServoConfigTests(unittest.TestCase):
    """Plan 2 keys: servo_pin, servo_bottom, servo_top, servo_step, servo_level,
    servo_dir, servo_settle, hold_sec (+ scaleMac -> mac)."""

    FULL = {"servo_pin": 10, "servo_bottom": 112.0, "servo_top": 85, "servo_step": 4.5,
            "servo_level": 90, "servo_dir": -1, "servo_settle": 200, "hold_sec": 12}

    def test_contract_keys_are_exactly_plan2(self):
        # Plan 2 contract keys + TV3 servo_ctrl.zip keys (servo_delay, servo_dps), 22/11.
        self.assertEqual(set(SERVO_LIMITS), {"servo_pin", "servo_bottom", "servo_top", "servo_step",
                                             "servo_level", "servo_dir", "servo_settle", "hold_sec",
                                             "servo_delay", "servo_dps"})

    def test_all_new_keys_serialize_in_order(self):
        self.assertEqual(config_commands(self.FULL),
                         b"BEGIN\nCFG servo_pin=10\nCFG servo_bottom=112\nCFG servo_top=85\n"
                         b"CFG servo_step=4.5\nCFG servo_level=90\nCFG servo_dir=-1\n"
                         b"CFG servo_settle=200\nCFG hold_sec=12\nAPPLY\n")

    def test_mixed_with_legacy_keys_keeps_legacy_bytes(self):
        out = config_commands({"ssid": "LAB", "mode": "ground", "tilt": 7.5, "hold_sec": 10.25})
        self.assertEqual(out, b"BEGIN\nCFG ssid=LAB\nCFG mode=ground\nCFG tilt=7.5\n"
                              b"CFG hold_sec=10.25\nAPPLY\n")

    def test_numeric_strings_from_cli_are_accepted(self):
        self.assertEqual(config_commands({"servo_pin": "10", "servo_step": "5.0"}),
                         b"BEGIN\nCFG servo_pin=10\nCFG servo_step=5\nAPPLY\n")

    def test_none_values_are_not_sent(self):
        self.assertEqual(config_commands({"servo_pin": None, "hold_sec": 15, "ssid": None}),
                         b"BEGIN\nCFG hold_sec=15\nAPPLY\n")
        self.assertEqual(config_commands({"servo_top": None}), b"BEGIN\nAPPLY\n")

    def test_scale_mac_alias_uses_existing_mac_key(self):
        self.assertEqual(config_commands({"scaleMac": "70:87:9e:ea:3a:e0"}),
                         b"BEGIN\nCFG mac=70:87:9e:ea:3a:e0\nAPPLY\n")
        self.assertEqual(config_commands({"mac": "70:87:9E:EA:3A:E0"}),
                         b"BEGIN\nCFG mac=70:87:9E:EA:3A:E0\nAPPLY\n")
        # Same value twice is sent once; different values are an error.
        self.assertEqual(config_commands({"mac": "AA:BB:CC:DD:EE:FF", "scaleMac": "AA:BB:CC:DD:EE:FF"}),
                         b"BEGIN\nCFG mac=AA:BB:CC:DD:EE:FF\nAPPLY\n")
        with self.assertRaises(ValueError):
            config_commands({"mac": "AA:BB:CC:DD:EE:FF", "scaleMac": "11:22:33:44:55:66"})
        for bad in ("70:87:9e:ea:3a", "70-87-9e-ea-3a-e0", "70:87:9e:ea:3a:zz", "70879eea3ae0", ""):
            with self.subTest(mac=bad), self.assertRaises(ValueError):
                config_commands({"scaleMac": bad})

    def test_out_of_range_and_wrong_types_are_rejected(self):
        bad = [("servo_pin", -1), ("servo_pin", 49), ("servo_pin", 4.5), ("servo_pin", "x"),
               ("servo_bottom", -0.1), ("servo_top", 180.5), ("servo_level", 200),
               ("servo_step", 0), ("servo_step", 0.4), ("servo_step", 21),
               ("servo_dir", 0), ("servo_dir", 2), ("servo_dir", -2), ("servo_dir", 0.5),
               ("servo_settle", -1), ("servo_settle", 3001), ("servo_settle", 150.5),
               ("hold_sec", -1), ("hold_sec", 121),
               ("hold_sec", float("nan")), ("hold_sec", float("inf")), ("hold_sec", True),
               ("servo_step", [4]), ("servo_level", "90deg")]
        for key, value in bad:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                config_commands({key: value})

    def test_reserved_gpio_accepted_only_range_checked(self):
        # Team decision 22/11 (like TV3's driver): reserved pins are not blocked.
        for pin in sorted(FORBIDDEN_SERVO_PINS):
            with self.subTest(pin=pin):
                self.assertEqual(config_commands({"servo_pin": pin}),
                                 f"BEGIN\nCFG servo_pin={pin}\nAPPLY\n".encode())
        for pin in (-1, 49):
            with self.subTest(pin=pin), self.assertRaises(ValueError):
                config_commands({"servo_pin": pin})

    def test_tv3_servo_delay_and_speed_keys(self):
        self.assertEqual(config_commands({"servo_delay": 500, "servo_dps": 4.0}),
                         b"BEGIN\nCFG servo_delay=500\nCFG servo_dps=4\nAPPLY\n")
        for key, bad in (("servo_delay", -1), ("servo_delay", 3001), ("servo_delay", 1.5),
                         ("servo_dps", 0), ("servo_dps", 61)):
            with self.subTest(key=key, bad=bad), self.assertRaises(ValueError):
                config_commands({key: bad})

    def test_free_gpio_accepted(self):
        for pin in (2, 6, 10, 21, 47, 48):
            with self.subTest(pin=pin):
                self.assertEqual(config_commands({"servo_pin": pin}),
                                 f"BEGIN\nCFG servo_pin={pin}\nAPPLY\n".encode())

    def test_bottom_and_top_must_differ(self):
        with self.assertRaises(ValueError):
            config_commands({"servo_bottom": 90, "servo_top": 90.0})
        self.assertIn(b"servo_top=91", config_commands({"servo_bottom": 90, "servo_top": 91}))

    def test_boundaries_are_inclusive(self):
        self.assertEqual(config_commands({"servo_bottom": 45, "servo_top": 135, "servo_step": 0.5,
                                          "servo_settle": 3000, "hold_sec": 0, "servo_dir": 1}),
                         b"BEGIN\nCFG servo_bottom=45\nCFG servo_top=135\nCFG servo_step=0.5\n"
                         b"CFG servo_settle=3000\nCFG hold_sec=0\nCFG servo_dir=1\nAPPLY\n")

    def test_servo_travel_is_45_to_135(self):
        for key in ("servo_bottom", "servo_top", "servo_level"):
            for bad in (0, 44.9, 135.1, 180):
                with self.subTest(key=key, bad=bad), self.assertRaises(ValueError):
                    config_commands({key: bad})

    def test_unknown_keys_still_rejected(self):
        for key in ("servo_speed", "servoPin", "hold", "SERVO_PIN"):
            with self.subTest(key=key), self.assertRaises(ValueError):
                config_commands({key: 1})


class CameraImageConfigTests(unittest.TestCase):
    """cam_framesize / cam_quality: pushed by the native UNO to the camera /control."""

    def test_valid_values_serialize_as_whole_numbers(self):
        self.assertEqual(config_commands({"cam_framesize": "5", "cam_quality": 12.0}),
                         b"BEGIN\nCFG cam_framesize=5\nCFG cam_quality=12\nAPPLY\n")
        self.assertEqual(config_commands({"cam_quality": -1}), b"BEGIN\nCFG cam_quality=-1\nAPPLY\n")

    def test_out_of_range_values_are_rejected_before_sending(self):
        for key, bad in (("cam_quality", 3), ("cam_quality", 0), ("cam_quality", 64),
                         ("cam_quality", 12.5), ("cam_framesize", -2), ("cam_framesize", 11),
                         ("cam_framesize", "x"), ("cam_framesize", True)):
            with self.subTest(key=key, bad=bad), self.assertRaises(ValueError):
                config_commands({key: bad})

    def test_servo_contract_keys_unchanged(self):
        self.assertNotIn("cam_quality", SERVO_LIMITS)


if __name__ == "__main__":
    unittest.main()
