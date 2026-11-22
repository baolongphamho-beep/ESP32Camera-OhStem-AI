from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "camera"))
from servo_bench import StreamDemux, format_packet_notice, parse_user_command
from usb_protocol import packet


class ServoBenchTests(unittest.TestCase):
    def test_stream_demux_separates_text_and_split_ssv1_packets(self):
        demux = StreamDemux()
        stream = (
            b"[SERVO] ready=1\r\n"
            + packet({"kind": "config", "ok": True})
            + b"[SERVO] Moved to 90.0 deg\r\n"
        )
        all_lines = []
        all_packets = []
        for idx in range(0, len(stream), 3):
            lines, packets = demux.feed(stream[idx:idx + 3])
            all_lines.extend(lines)
            all_packets.extend(packets)
        self.assertEqual(all_lines, ["[SERVO] ready=1", "[SERVO] Moved to 90.0 deg"])
        self.assertEqual(all_packets, [{"kind": "config", "ok": True}])

    def test_stream_demux_handles_partial_magic_not_followed_by_packet(self):
        demux = StreamDemux()
        lines1, pkts1 = demux.feed(b"text SS")
        self.assertEqual((lines1, pkts1), ([], []))
        lines2, pkts2 = demux.feed(b"ERVO\r\n")
        self.assertEqual(lines2, ["text SSERVO"])
        self.assertEqual(pkts2, [])

    def test_parse_user_command_shortcuts_angles_and_config(self):
        self.assertEqual(parse_user_command("   "), ("empty", None))
        self.assertEqual(parse_user_command("quit"), ("quit", None))
        self.assertEqual(parse_user_command("Q"), ("quit", None))
        self.assertEqual(parse_user_command("90"), ("raw", b"SERVO 90\n"))
        self.assertEqual(parse_user_command("68.5"), ("raw", b"SERVO 68.5\n"))
        self.assertEqual(parse_user_command("sweep"), ("raw", b"SERVO SWEEP\n"))
        self.assertEqual(parse_user_command("detach"), ("raw", b"SERVO DETACH\n"))
        self.assertEqual(parse_user_command("status"), ("raw", b"SERVO STATUS\n"))
        self.assertEqual(
            parse_user_command("set servo_level=92 servo_dir=-1"),
            ("config", b"BEGIN\nCFG servo_level=92\nCFG servo_dir=-1\nAPPLY\n"),
        )
        for bad in ("set", "set missing_eq", "set bad_key=1", "set =10"):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    parse_user_command(bad)

    def test_format_packet_notice(self):
        self.assertEqual(format_packet_notice({"kind": "config", "ok": True}), "[CONFIG] Saved to NVS (OK)")
        self.assertEqual(format_packet_notice({"kind": "config", "ok": False}), "[CONFIG] Rejected by firmware")
        self.assertEqual(format_packet_notice({"kind": "status", "message": "ok"}), "[UNO] ok")
        self.assertIsNone(format_packet_notice({"kind": "scale", "phase": "STANDBY"}))


if __name__ == "__main__":
    unittest.main()
