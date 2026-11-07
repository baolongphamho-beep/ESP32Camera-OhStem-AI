import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "camera"))
from usb_protocol import HEADER, Parser, config_commands, packet


class USBProtocolTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
