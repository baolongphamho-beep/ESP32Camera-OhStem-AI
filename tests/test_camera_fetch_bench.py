import http.server
import json
import pathlib
import sys
import tempfile
import threading
import unittest
import urllib.parse
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import camera_fetch_bench as bench  # noqa: E402


def fake_jpeg(width, height, quality):
    """Minimal JPEG-shaped bytes: SOI, APP0, SOF0 with the size, padding, EOI."""
    app0 = b"\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    sof = b"\xff\xc0\x00\x11\x08" + height.to_bytes(2, "big") + width.to_bytes(2, "big") + b"\x03" + b"\x00" * 9
    body = b"\x00" * (width * height // max(quality, 1))  # lower quality number = bigger
    return b"\xff\xd8" + app0 + sof + body + b"\xff\xd9"


class FakeCamera(http.server.BaseHTTPRequestHandler):
    state = {"framesize": 8, "quality": 15}
    log = []

    def do_GET(self):  # noqa: N802
        url = urllib.parse.urlsplit(self.path)
        query = dict(urllib.parse.parse_qsl(url.query))
        FakeCamera.log.append(self.path)
        if url.path == "/status":
            body = json.dumps(self.state).encode()
        elif url.path == "/control":
            self.state[query["var"]] = int(query["val"])
            body = b""
        elif url.path == "/capture":
            w, h = bench.KNOWN_SIZES[self.state["framesize"]]
            body = fake_jpeg(w, h, self.state["quality"])
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


class CameraFetchBenchTests(unittest.TestCase):
    def test_base_url_normalizes_and_rejects_https(self):
        self.assertEqual(bench.base_url("10.127.7.68"), "http://10.127.7.68")
        self.assertEqual(bench.base_url("http://10.127.7.68/capture"), "http://10.127.7.68")
        self.assertEqual(bench.base_url("http://cam:8080/x?y=1"), "http://cam:8080")
        with self.assertRaises(ValueError):
            bench.base_url("https://cam")

    def test_jpeg_size_reads_sof_and_rejects_non_jpeg(self):
        self.assertEqual(bench.jpeg_size(fake_jpeg(320, 240, 15)), (320, 240))
        self.assertIsNone(bench.jpeg_size(b'{"busy":1}'))
        self.assertIsNone(bench.jpeg_size(b"\xff\xd8\xff\xe0\x00\x01"))

    def test_jpeg_size_on_a_real_encoder_output(self):
        try:
            import cv2
            import numpy as np
        except ImportError:
            self.skipTest("OpenCV not installed")
        ok, data = cv2.imencode(".jpg", np.zeros((296, 400, 3), np.uint8), [cv2.IMWRITE_JPEG_QUALITY, 80])
        self.assertTrue(ok)
        self.assertEqual(bench.jpeg_size(data.tobytes()), (400, 296))

    def test_measure_counts_failures_and_flags_unexpected_resolution(self):
        replies = iter([OSError("timeout"),                           # warm-up failure: ignored
                        (200, fake_jpeg(240, 240, 15), 100.0),        # framesize 5 but 240x240
                        (200, b'{"busy":1}', 50.0),                    # not a JPEG: failure
                        (200, fake_jpeg(240, 240, 15), 300.0)])

        def get(url, timeout):
            reply = next(replies)
            if isinstance(reply, Exception):
                raise reply
            return reply
        with mock.patch("sys.stderr"):
            row = bench.measure("http://cam", 5, 15, frames=3, timeout=1, warmup=1, pause=0, get=get)
        self.assertEqual((row["frames"], row["failures"]), (2, 1))
        self.assertEqual(row["fetch_ms_median"], 200)
        self.assertEqual(row["resolution"], "240x240")
        self.assertIn("other framesize enum", row["note"])

    def test_recommend_prefers_sharpest_within_slack(self):
        rows = [
            {"framesize": 5, "quality": 15, "failures": 0, "resolution": "320x240", "fetch_ms_median": 200},
            {"framesize": 5, "quality": 10, "failures": 0, "resolution": "320x240", "fetch_ms_median": 230},
            {"framesize": 7, "quality": 12, "failures": 0, "resolution": "480x320", "fetch_ms_median": 400},
            {"framesize": 6, "quality": 12, "failures": 1, "resolution": "400x296", "fetch_ms_median": 210},
        ]
        best = bench.recommend(rows)
        self.assertEqual((best["framesize"], best["quality"]), (5, 10))
        self.assertIsNone(bench.recommend([{"framesize": 5, "quality": 15, "failures": 2,
                                            "resolution": None}]))

    def test_end_to_end_against_fake_camera_restores_settings(self):
        FakeCamera.state = {"framesize": 8, "quality": 15}
        FakeCamera.log = []
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FakeCamera)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory() as folder, mock.patch("builtins.print"), \
                    mock.patch.object(bench.time, "sleep"):
                out = pathlib.Path(folder) / "bench.json"
                code = bench.main(["--camera", f"http://127.0.0.1:{server.server_port}/capture",
                                   "--framesize", "5", "7", "--quality", "15", "10",
                                   "--frames", "3", "--warmup", "1", "--json", str(out)])
                report = json.loads(out.read_text(encoding="utf-8"))
        finally:
            server.shutdown()
            server.server_close()
        self.assertEqual(code, 0)
        self.assertEqual(len(report["rows"]), 4)
        self.assertEqual({r["resolution"] for r in report["rows"]}, {"320x240", "480x320"})
        self.assertTrue(all(r["frames"] == 3 and r["failures"] == 0 for r in report["rows"]))
        self.assertEqual(report["original"], {"framesize": 8, "quality": 15})
        self.assertEqual(FakeCamera.state, {"framesize": 8, "quality": 15})  # restored
        self.assertIsNotNone(report["recommended"])

    def test_invalid_arguments_exit_before_contacting_camera(self):
        for argv in (["--camera", "http://cam", "--quality", "3"],
                     ["--camera", "http://cam", "--framesize", "11"],
                     ["--camera", "ftp://cam"]):
            with self.subTest(argv=argv), mock.patch("sys.stderr"), \
                    mock.patch.object(bench, "http_get", side_effect=AssertionError("network")), \
                    self.assertRaises(SystemExit):
                bench.main(argv)


if __name__ == "__main__":
    unittest.main()
