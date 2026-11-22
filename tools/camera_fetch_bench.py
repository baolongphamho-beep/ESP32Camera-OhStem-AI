"""Measure OhStem camera /capture latency vs image settings, from the laptop.

Answers "how sharp can the image be before fetching gets slow?" BEFORE changing the
UNO firmware or model. Each (framesize, quality) pair is set with /control, then
/capture is fetched --frames times; median/p90 fetch time, JPEG size and the real
resolution are reported. The camera's original framesize/quality (from /status) are
restored at the end. Nothing is flashed; only runtime settings are changed.

  py -3 tools/camera_fetch_bench.py --camera http://10.127.7.68
  py -3 tools/camera_fetch_bench.py --camera http://10.127.7.68 --framesize 5 6 7 --quality 15 12 10 \
      --frames 15 --json .edge-deps/qa/camera-bench.json

Run it while the UNO is NOT fetching (STANDBY, viewer closed) and no browser tab has the
camera page open: one camera serves one client at a time and would skew the numbers.
The laptop's Wi-Fi differs from the UNO's (antenna, BLE coexistence): use the table to
compare settings, then confirm the chosen pair with usb_viewer.py --preview --log.
"""
import argparse
import json
import statistics
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# esp32-camera framesize enum WITH 240x240 (the LAB camera: 5 = 320x240, 8 = 640x480 seen).
KNOWN_SIZES = {0: (96, 96), 1: (160, 120), 2: (176, 144), 3: (240, 176), 4: (240, 240),
               5: (320, 240), 6: (400, 296), 7: (480, 320), 8: (640, 480), 9: (800, 600),
               10: (1024, 768)}


def base_url(camera):
    """'10.127.7.68', 'http://10.127.7.68/capture' -> 'http://10.127.7.68'."""
    if "://" not in camera:
        camera = "http://" + camera
    parts = urllib.parse.urlsplit(camera)
    if parts.scheme != "http" or not parts.netloc:
        raise ValueError("camera must be an http:// address, e.g. http://10.127.7.68")
    return f"http://{parts.netloc}"


def jpeg_size(data):
    """(width, height) from the JPEG SOF marker, or None if not a JPEG."""
    if len(data) < 4 or data[:2] != b"\xff\xd8":
        return None
    i = 2
    while i + 9 < len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7 or marker == 0xFF:
            i += 1 if marker == 0xFF else 2
            continue
        length = int.from_bytes(data[i + 2:i + 4], "big")
        if marker in (0xC0, 0xC1, 0xC2):
            height = int.from_bytes(data[i + 5:i + 7], "big")
            width = int.from_bytes(data[i + 7:i + 9], "big")
            return width, height
        if length < 2:
            return None
        i += 2 + length
    return None


def http_get(url, timeout):
    start = time.perf_counter()
    with urllib.request.urlopen(url, timeout=timeout) as reply:
        body = reply.read()
        status = reply.status
    return status, body, (time.perf_counter() - start) * 1000


def read_status(base, timeout):
    try:
        status, body, _ = http_get(base + "/status", timeout)
        if status == 200:
            data = json.loads(body)
            if isinstance(data, dict):
                return {k: data[k] for k in ("framesize", "quality") if isinstance(data.get(k), int)}
    except (OSError, ValueError, urllib.error.URLError):
        pass
    return {}


def set_var(base, var, value, timeout):
    status, _, _ = http_get(f"{base}/control?var={var}&val={int(value)}", timeout)
    if status != 200:
        raise OSError(f"/control {var}={value} -> HTTP {status}")


def percentile(values, q):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(q * (len(ordered) - 1))))]


def measure(base, framesize, quality, frames, timeout, warmup, pause, get=http_get):
    """Fetch `frames` images (after `warmup` discarded ones) at the current settings."""
    times, sizes, failures, resolution = [], [], 0, None
    for i in range(warmup + frames):
        try:
            status, body, ms = get(base + "/capture", timeout)
            dims = jpeg_size(body) if status == 200 else None
            if dims is None:
                raise OSError(f"HTTP {status}, not a JPEG ({len(body)} B)")
        except (OSError, urllib.error.URLError) as error:
            if i >= warmup:
                failures += 1
                print(f"  capture failed: {error}", file=sys.stderr)
            continue
        if i >= warmup:
            times.append(ms)
            sizes.append(len(body))
            resolution = dims
        if pause:
            time.sleep(pause)
    row = {"framesize": framesize, "quality": quality, "frames": len(times), "failures": failures,
           "resolution": f"{resolution[0]}x{resolution[1]}" if resolution else None}
    if times:
        row.update(fetch_ms_min=round(min(times)), fetch_ms_median=round(statistics.median(times)),
                   fetch_ms_p90=round(percentile(times, .9)), fetch_ms_max=round(max(times)),
                   jpeg_kb_median=round(statistics.median(sizes) / 1024, 1))
    expected = KNOWN_SIZES.get(framesize)
    if resolution and expected and tuple(resolution) != expected:
        row["note"] = (f"camera returned {row['resolution']} for framesize={framesize}, expected "
                       f"{expected[0]}x{expected[1]}: other framesize enum, check before using")
    return row


def recommend(rows, slack=1.25):
    """Sharpest pair (largest image, then lowest quality number) whose median fetch is
    within `slack` x the fastest median and that had no failures."""
    ok = [r for r in rows if r.get("fetch_ms_median") is not None and not r["failures"]]
    if not ok:
        return None
    fastest = min(r["fetch_ms_median"] for r in ok)
    within = [r for r in ok if r["fetch_ms_median"] <= fastest * slack]

    def pixels(r):
        w, _, h = (r["resolution"] or "0x0").partition("x")
        return int(w) * int(h)
    return max(within, key=lambda r: (pixels(r), -r["quality"]))


def print_table(rows):
    print(f"{'size':>5} {'q':>3} {'resolution':>10} {'ok':>3} {'fail':>4} {'med ms':>7} {'p90 ms':>7} "
          f"{'max ms':>7} {'KB':>6}")
    for r in rows:
        print(f"{r['framesize']:>5} {r['quality']:>3} {r['resolution'] or '--':>10} {r['frames']:>3} "
              f"{r['failures']:>4} {r.get('fetch_ms_median', '--'):>7} {r.get('fetch_ms_p90', '--'):>7} "
              f"{r.get('fetch_ms_max', '--'):>7} {r.get('jpeg_kb_median', '--'):>6}")
        if r.get("note"):
            print(f"      ! {r['note']}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--camera", required=True, help="Camera address, e.g. http://10.127.7.68")
    ap.add_argument("--framesize", type=int, nargs="+", default=[5, 6, 7])
    ap.add_argument("--quality", type=int, nargs="+", default=[15, 12, 10])
    ap.add_argument("--frames", type=int, default=10, help="Measured captures per pair")
    ap.add_argument("--warmup", type=int, default=2, help="Discarded captures after each change")
    ap.add_argument("--pause", type=float, default=0.0, help="Seconds between captures")
    ap.add_argument("--timeout", type=float, default=8.0)
    ap.add_argument("--json", help="Write the result table as JSON (e.g. .edge-deps/qa/camera-bench.json)")
    ap.add_argument("--no-restore", action="store_true", help="Leave the last tested setting on the camera")
    args = ap.parse_args(argv)
    try:
        base = base_url(args.camera)
    except ValueError as error:
        ap.error(str(error))
    if args.frames < 1 or any(not 0 <= f <= 10 for f in args.framesize) or \
            any(not 4 <= q <= 63 for q in args.quality):
        ap.error("frames >= 1, framesize 0..10, quality 4..63")
    original = read_status(base, args.timeout)
    print(f"Camera {base}; original settings: {original or 'unknown (/status unreadable)'}")
    rows = []
    try:
        for framesize in args.framesize:
            for quality in args.quality:
                print(f"framesize={framesize} quality={quality} ...", flush=True)
                try:
                    set_var(base, "framesize", framesize, args.timeout)
                    set_var(base, "quality", quality, args.timeout)
                except (OSError, urllib.error.URLError) as error:
                    print(f"  cannot apply setting: {error}", file=sys.stderr)
                    rows.append({"framesize": framesize, "quality": quality, "frames": 0,
                                 "failures": args.frames, "resolution": None, "note": str(error)})
                    continue
                time.sleep(0.3)  # Sensor re-init after a size change.
                rows.append(measure(base, framesize, quality, args.frames, args.timeout,
                                    args.warmup, args.pause))
    except KeyboardInterrupt:
        print("Interrupted; restoring settings.", file=sys.stderr)
    finally:
        if original and not args.no_restore:
            try:
                for var in ("framesize", "quality"):
                    if var in original:
                        set_var(base, var, original[var], args.timeout)
                print(f"Restored {original}.")
            except (OSError, urllib.error.URLError) as error:
                print(f"Could not restore {original}: {error}", file=sys.stderr)
        elif not original:
            print("Original settings unknown: set framesize/quality again in the camera page if needed.")
    print_table(rows)
    best = recommend(rows)
    if best:
        print(f"\nSharpest pair within 25% of the fastest median: framesize={best['framesize']} "
              f"quality={best['quality']} ({best['resolution']}, {best['fetch_ms_median']} ms, "
              f"{best['jpeg_kb_median']} KB).")
        print(f"Try on the UNO: usb_viewer.py --preview --set cam_framesize={best['framesize']} "
              f"--set cam_quality={best['quality']} --log .edge-deps/qa/camera.jsonl")
        print("Laptop numbers are a comparison only; the UNO's capture_ms is the value to report.")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as out:
            json.dump({"camera": base, "original": original, "rows": rows, "recommended": best}, out, indent=2)
    return 0 if best else 1


if __name__ == "__main__":
    sys.exit(main())
