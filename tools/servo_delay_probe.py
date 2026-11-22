"""Measure how stale the OhStem camera image is after the servo stops (-> servo_delay).

TV3 estimated ~500 ms ("the camera returns images ~this old"). This measures it:
  1. the UNO (native firmware, USB "SERVO <deg>") holds angle A; the laptop grabs
     reference frames from http://<camera>/capture;
  2. the UNO moves to B; the laptop keeps fetching as fast as it can and timestamps the
     START of each request;
  3. each frame is compared with the final frame at B. The last request that still shows
     an older picture, minus the time the servo stopped (command + servoTravelMs),
     is the image age. servo_delay should cover the worst case + a margin.

  py -3 tools/servo_delay_probe.py --camera http://10.127.7.68 --a 85 --b 95 --cycles 6

Needs the camera on Wi-Fi, the UNO on USB (Serial Monitor/viewer closed) and the servo
powered. While it runs the UNO is in SERVO_JOG (no capture of its own), so the camera
serves only this laptop. Point the camera at a scene with texture (not a blank wall).
"""
import argparse
import json
import math
import statistics
import sys
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "camera"))
from serial_port import add_port_argument, port_or_exit  # noqa: E402
from usb_protocol import SERVO_TRAVEL  # noqa: E402

MOVE_DPS, ACCEL_DPS2 = 60.0, 240.0  # lib/EdgeSweep/servo_motion.h (kServoMoveDps / Accel)


def travel_ms(dist, v=MOVE_DPS, a=ACCEL_DPS2):
    """Same profile as servoTravelMs() in servo_motion.h (rest to rest, + one 20 ms tick)."""
    dist = abs(dist)
    if dist < 0.02:
        return 0
    t = dist / v + v / a if dist > v * v / a else 2 * math.sqrt(dist / a)
    return math.ceil(t * 1000) + 20


def frame_diff(a, b):
    """Mean absolute difference of two small grayscale frames (numpy arrays)."""
    import numpy as np
    return float(np.mean(np.abs(a.astype(np.int16) - b.astype(np.int16))))


def estimate_age(samples, stop_ms, threshold):
    """samples: [(request_start_ms, diff_to_final)], time-ordered after the move command.

    Returns the image age (ms) = start of the LAST request still showing an older picture
    (diff > threshold) minus the servo stop time; 0 if the first request after the stop is
    already current; None if the picture never settled (diff stays high to the end).
    """
    if not samples or samples[-1][1] > threshold:
        return None
    stale = [t for t, d in samples if d > threshold]
    if not stale:
        return 0
    return max(0, round(stale[-1] - stop_ms))


def recommend(ages, margin_ms=100):
    """servo_delay suggestion: worst measured age + margin, rounded up to 50 ms, capped 3000."""
    valid = [a for a in ages if a is not None]
    if not valid:
        return None
    return int(min(3000, math.ceil((max(valid) + margin_ms) / 50.0) * 50))


def grab(url, timeout):
    import cv2
    import numpy as np
    with urllib.request.urlopen(url, timeout=timeout) as reply:
        data = reply.read()
    image = cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_GRAYSCALE)
    if image is None:
        raise OSError(f"not a JPEG ({len(data)} B)")
    return cv2.resize(image, (80, 60), interpolation=cv2.INTER_AREA)


def main():
    import serial

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--camera", required=True, help="e.g. http://10.127.7.68 (or .../capture)")
    add_port_argument(ap)
    ap.add_argument("--a", type=float, default=85.0, help="servo angle A (45..135)")
    ap.add_argument("--b", type=float, default=95.0, help="servo angle B (45..135)")
    ap.add_argument("--cycles", type=int, default=6, help="moves to measure (A->B, B->A, ...)")
    ap.add_argument("--watch", type=float, default=3.0, help="seconds of fetching after each move")
    ap.add_argument("--timeout", type=float, default=6.0)
    ap.add_argument("--json", help="write raw samples and results (e.g. .edge-deps/qa/servo-delay.json)")
    args = ap.parse_args()
    low, high = SERVO_TRAVEL
    if not (low <= args.a <= high and low <= args.b <= high) or abs(args.a - args.b) < 3:
        ap.error("--a/--b must be in 45..135 and at least 3 deg apart")
    url = args.camera if "/capture" in args.camera else args.camera.rstrip("/") + "/capture"
    if not url.startswith("http://"):
        url = "http://" + url
    port = port_or_exit(args.port)
    results = []
    with serial.Serial(port, 115200, timeout=0.05, write_timeout=3) as dev:
        current = args.a

        def hold(deg):
            dev.write(f"SERVO {deg:g}\n".encode())
            dev.flush()

        try:
            hold(current)
            time.sleep(2.5)  # first move after boot may be a snap; let it settle
            for cycle in range(args.cycles):
                target = args.b if current == args.a else args.a
                ref = [grab(url, args.timeout) for _ in range(3)]
                noise = max(frame_diff(ref[0], ref[1]), frame_diff(ref[1], ref[2]))
                t0 = time.monotonic() * 1000
                hold(target)
                stop_ms = t0 + travel_ms(target - current)
                frames, renew = [], t0 + 2500
                while time.monotonic() * 1000 - t0 < args.watch * 1000:
                    started = time.monotonic() * 1000
                    if started >= renew:
                        hold(target)  # keep the jog lease alive
                        renew = started + 2500
                    try:
                        frames.append((started, grab(url, args.timeout)))
                    except OSError as error:
                        print(f"  capture failed: {error}", file=sys.stderr)
                if len(frames) < 3:
                    print(f"cycle {cycle + 1}: too few frames ({len(frames)}), skipped")
                    current = target
                    continue
                final = frames[-1][1]
                change = frame_diff(ref[-1], final)
                threshold = max(1.0, 3 * noise, 0.3 * change)
                samples = [(t, frame_diff(img, final)) for t, img in frames]
                age = estimate_age(samples, stop_ms, threshold)
                fetch = statistics.median(b - a for (a, _), (b, _) in zip(frames, frames[1:]))
                print(f"cycle {cycle + 1}: {current:g} -> {target:g} deg, scene change {change:.1f} "
                      f"(noise {noise:.1f}), {len(frames)} frames, fetch ~{fetch:.0f} ms, "
                      f"image age after stop: {age if age is not None else 'not settled'} ms")
                if change < 3 * noise or change < 2:
                    print("  ! scene barely changes with the servo: aim the camera at something textured")
                results.append({"from": current, "to": target, "noise": noise, "change": change,
                                "threshold": threshold, "stop_ms": stop_ms - t0, "age_ms": age,
                                "samples": [(round(t - t0), round(d, 2)) for t, d in samples]})
                current = target
        finally:
            dev.write(b"SERVO OFF\n")
            dev.flush()
    ages = [r["age_ms"] for r in results]
    best = recommend(ages)
    valid = [a for a in ages if a is not None]
    if valid:
        print(f"\nImage age after the servo stops: median {statistics.median(valid):.0f} ms, max {max(valid)} ms "
              f"({len(valid)}/{len(ages)} cycles).")
        print(f"Suggested: py -3 camera\\usb_viewer.py --set servo_delay={best}")
        print("(Laptop fetch timing; the UNO fetches the same camera, so the age is similar but not identical.)")
    else:
        print("\nNo cycle settled: check the camera URL, the servo power and that the view changes when it moves.")
    if args.json:
        Path(args.json).parent.mkdir(parents=True, exist_ok=True)
        Path(args.json).write_text(json.dumps({"camera": url, "results": results, "suggested_servo_delay": best},
                                              indent=1), encoding="utf-8")
    return 0 if best is not None else 1


if __name__ == "__main__":
    sys.exit(main())
