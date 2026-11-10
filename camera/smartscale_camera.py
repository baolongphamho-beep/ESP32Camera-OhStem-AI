"""OhStem image -> laptop YOLO -> immediate geometry -> UART -> LCD/BMI."""
import argparse
import json
import math
from pathlib import Path
import time

from geometry import calibrate_focal, fixed_distance_height, ground_plane_height, measurement_label
from protocol import serial_control_url

DIRECTORY = Path(__file__).resolve().parent


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}


def configure(settings):
    print("Camera setup. Dimensions refer to the TOP of the scale, not room floor.")
    print("fixed: level camera, known distance. ground: known mounting height/tilt.")
    mode = input(f"Mode fixed/ground [{settings.get('mode', 'fixed')}]: ").strip() or settings.get("mode", "fixed")
    if mode not in ("fixed", "ground"):
        raise ValueError("Choose fixed or ground")
    result = {"mode": mode}

    def number(key, prompt, optional=False, default=None):
        previous = settings.get(key, default)
        suffix = " (type - to clear)" if optional else ""
        value = input(f"{prompt}{suffix} [{previous if previous is not None else 'unknown'}]: ").strip()
        result[key] = None if optional and value == "-" else (float(value) if value else previous)
        if not optional and result[key] is None:
            raise ValueError(f"{key} is required")

    number("focal_px", "Calibrated fy in pixels at capture resolution (blank uses saved focal)", True)
    if mode == "fixed":
        number("distance_m", "Measured camera-to-scale distance in metres")
        number("reference_cm", "Actual FIRST reference person height in cm (blank if focal known)", True)
        result["tilt_deg"] = 0
    else:
        number("camera_height_cm", "Camera centre above scale top in cm")
        number("tilt_deg", "Camera tilt DOWN from horizontal in degrees", default=0)
    return result


def resolve_focal(settings, calibration, resolution, pixel_height):
    focal = settings.get("focal_px")
    if focal is not None:
        if not math.isfinite(focal) or focal <= 0:
            raise ValueError("focal_px must be positive")
        return focal, None
    if calibration.get("schema") == 2 and calibration.get("resolution") == list(resolution):
        focal = float(calibration["focal_px"])
        if math.isfinite(focal) and focal > 0:
            return focal, None
    if calibration.get("schema") == 2 and calibration.get("resolution") != list(resolution):
        raise ValueError("resolution changed: restart with --recalibrate and the reference person")
    if settings["mode"] == "fixed" and settings.get("reference_cm") is not None:
        focal = calibrate_focal(pixel_height, settings.get("distance_m"), settings["reference_cm"])
        return focal, {"schema": 2, "focal_px": focal, "resolution": list(resolution),
                       "reference_cm": settings["reference_cm"], "reference_distance_m": settings["distance_m"]}
    raise ValueError("Need focal calibration: fixed mode + known distance + reference height")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", required=True, help="Camera IP on same Wi-Fi")
    local_model = DIRECTORY.parent / "yolo26n.pt"
    repository_model = DIRECTORY.parent.parent / "yolo26n.pt"
    sibling_model = DIRECTORY.parent.parent / "ESP32Camera-OhStem-AI/yolo26n.pt"
    if local_model.is_file():
        default_model = local_model
    elif repository_model.is_file():
        default_model = repository_model
    else:
        default_model = sibling_model
    parser.add_argument("--model", type=Path, default=default_model)
    parser.add_argument("--settings", type=Path, default=DIRECTORY / "settings.json")
    parser.add_argument("--calibration", type=Path, default=DIRECTORY / "calibration.json")
    parser.add_argument("--configure", action="store_true", help="Enter and save camera mounting parameters")
    parser.add_argument("--recalibrate", action="store_true", help="Ignore saved focal; calibrate on first full box")
    parser.add_argument("--mode", choices=["fixed", "ground"])
    parser.add_argument("--distance-m", type=float)
    parser.add_argument("--reference-cm", type=float)
    parser.add_argument("--focal-px", type=float, help="fy in PIXELS at capture resolution, not lens focal mm")
    parser.add_argument("--camera-height-cm", type=float)
    parser.add_argument("--tilt-deg", type=float)
    parser.add_argument("--send-uart", action="store_true")
    parser.add_argument("--fps", type=float, default=3, help="Maximum image requests/second (default 3)")
    parser.add_argument("--send-interval", type=float, default=1, help="Seconds between UART forwarding requests")
    parser.add_argument("--capture-timeout", type=float, default=4, help="Image HTTP read timeout in seconds")
    parser.add_argument("--control-timeout", type=float, default=2, help="Control HTTP timeout in seconds")
    args = parser.parse_args()
    if not math.isfinite(args.fps) or not 0.25 <= args.fps <= 10:
        parser.error("--fps must be between 0.25 and 10")
    for name in ("send_interval", "capture_timeout", "control_timeout"):
        if not math.isfinite(getattr(args, name)) or getattr(args, name) <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive and finite")
    settings = {"mode": "fixed", "tilt_deg": 0, **read_json(args.settings)}
    for name in ("mode", "distance_m", "reference_cm", "focal_px", "camera_height_cm", "tilt_deg"):
        if getattr(args, name) is not None:
            settings[name] = getattr(args, name)
    if args.configure:
        settings = configure(settings)
        args.settings.parent.mkdir(parents=True, exist_ok=True)
        args.settings.write_text(json.dumps(settings, indent=2), encoding="utf-8")
        print(f"[Setup] Saved {args.settings}")
    if settings["mode"] not in ("fixed", "ground"):
        parser.error("Invalid mode")
    for name in ("distance_m", "reference_cm", "focal_px", "camera_height_cm"):
        value = settings.get(name)
        if value is not None and (not math.isfinite(value) or value <= 0):
            parser.error(f"{name} must be positive and finite")
    if not math.isfinite(settings.get("tilt_deg", 0)) or abs(settings.get("tilt_deg", 0)) >= 80:
        parser.error("tilt_deg must be between -80 and 80")
    if settings["mode"] == "fixed" and settings.get("tilt_deg", 0) != 0:
        parser.error("fixed mode needs a level camera; use ground mode for tilted camera")
    if not args.model.is_file():
        parser.error(f"Model not found: {args.model}; set --model")

    import cv2
    import numpy as np
    import requests
    from ultralytics import YOLO

    calibration = {} if args.recalibrate else read_json(args.calibration)
    model = YOLO(str(args.model))
    base = "http://" + args.ip.strip().removeprefix("http://").rstrip("/")
    session = requests.Session()
    last_send = 0.0
    last_status = None
    next_capture_at = 0.0
    capture_errors = 0
    print(f"[Camera] {base}/capture | mode={settings['mode']}")
    print("Immediate result on first valid box. Q: quit. No C key / 15-frame wait.")
    print("Reference calibration: FIRST person must have the specified measured height.")
    print("Close browser Face/No Face tabs to avoid competing UART results.")
    print("[UART] HTTP forwarding ENABLED" if args.send_uart else
          "[UART WARNING] Forwarding DISABLED: LCD cannot receive detections. Add --send-uart.")
    print(f"[Network] Max capture {args.fps:g} fps; forward every {args.send_interval:g}s; retry with backoff")

    def send(label, confidence=0.0, force=False):
        nonlocal last_send
        if not args.send_uart or (not force and time.monotonic() - last_send < args.send_interval):
            return
        last_send = time.monotonic()
        try:
            reply = session.get(serial_control_url(base, label, confidence), timeout=args.control_timeout)
            reply.raise_for_status()
            print(f"[Camera HTTP accepted] {label}; verify UART/LCD on UNO")
        except requests.RequestException as exc:
            print(f"[UART forwarding ERROR] {exc}")

    try:
        while True:
            remaining = next_capture_at - time.monotonic()
            if remaining > 0:
                if cv2.waitKey(min(100, max(1, int(remaining * 1000)))) & 0xFF == ord("q"):
                    break
                continue
            next_capture_at = time.monotonic() + 1 / args.fps
            try:
                response = session.get(base + "/capture", params={"_cb": time.time_ns()}, timeout=(2, args.capture_timeout))
                response.raise_for_status()
                frame = cv2.imdecode(np.frombuffer(response.content, np.uint8), cv2.IMREAD_COLOR)
                if frame is None:
                    raise ValueError("/capture did not return an image")
            except (requests.RequestException, ValueError) as exc:
                capture_errors += 1
                retry_in = min(3.0, 0.5 * 2 ** min(capture_errors, 3))
                next_capture_at = time.monotonic() + retry_in
                print(f"[Camera ERROR #{capture_errors}] retry in {retry_in:g}s: {exc}")
                # A failed image request does not mean an observed empty scene.
                # Do not add control traffic to an unreachable/busy camera or resend
                # stale measurements; UNO expires the last camera result itself.
                offline = np.zeros((240, 640, 3), dtype=np.uint8)
                cv2.putText(offline, "Camera disconnected / retrying", (12, 45),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 0, 255), 2)
                cv2.putText(offline, "No fresh measurement. Check Wi-Fi / camera power.", (12, 80),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)
                cv2.imshow("SmartScale - Height and distance estimates", offline)
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break
                continue

            if capture_errors:
                print(f"[Camera] Image stream recovered after {capture_errors} failed requests")
                capture_errors = 0

            h, w = frame.shape[:2]
            boxes = model(frame, classes=[0], conf=0.6, verbose=False)[0].boxes
            label, confidence = "NO_PEOPLE", 0.0
            status = "No people detected"
            for box in boxes:
                x1, y1, x2, y2 = map(int, box.xyxy[0].tolist())
                cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
            if len(boxes) > 1:
                label, status = "MULTIPLE_PEOPLE", "Multiple people: stand on scale one at a time"
            elif len(boxes) == 1:
                box = tuple(map(float, boxes.xyxy[0].tolist()))
                x1, y1, x2, y2 = box
                confidence = float(boxes.conf[0])
                label = "PERSON_UNCALIBRATED"
                if x1 <= 0.5 or y1 <= 0.5 or x2 >= w - 0.5 or y2 >= h - 0.5:
                    label, status = "PERSON_PARTIAL", "Full body needed: box touches image edge"
                else:
                    try:
                        focal, new_calibration = resolve_focal(settings, calibration, (w, h), y2 - y1)
                        if settings["mode"] == "fixed":
                            height_cm, distance = fixed_distance_height(y2 - y1, focal, settings.get("distance_m"))
                            source = "fixed D"
                        else:
                            height_cm, distance = ground_plane_height(box, (w, h), focal,
                                settings.get("camera_height_cm"), settings.get("tilt_deg", 0))
                            source = "ground plane"
                        if new_calibration:
                            args.calibration.parent.mkdir(parents=True, exist_ok=True)
                            args.calibration.write_text(json.dumps(new_calibration, indent=2), encoding="utf-8")
                            calibration = new_calibration
                            print(f"[Calibration] Auto saved on FIRST box: fy={focal:.2f}px, {w}x{h}")
                        label = measurement_label(height_cm, distance)
                        status = f"H:{height_cm:.1f}cm D:{distance:.2f}m ({source})"
                    except (ValueError, TypeError, KeyError) as exc:
                        status = f"Person detected | Setup needed: {exc}"
            if status != last_status:
                print(f"[Measurement] {status}")
                last_status = status
            send(label, confidence)
            cv2.putText(frame, status, (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.48, (0, 255, 255), 1)
            cv2.putText(frame, "UART HTTP: ON (verify UNO RX)" if args.send_uart else "UART: OFF - add --send-uart",
                        (10, 45), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 255, 255), 1)
            cv2.imshow("SmartScale - Height and distance estimates", frame)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break
    finally:
        send("NO_PEOPLE", force=True)
        session.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
