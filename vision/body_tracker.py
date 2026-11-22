"""Laptop side of the Plan 2 servo sweep (env:yolo_uno_servo_sweep).

Each frame: grab an image from the OhStem camera (/capture), find people with YOLO
(yolo26n.pt, class person), send BOX_x1_y1_x2_y2_conf / NOBODY / MULTI to /control.
The camera forwards it over UART to the YOLO UNO, which moves the servo step by step,
decides when the whole body is in the frame, and computes height + BMI.

Examples (repo root):
  .\\.venv-camera\\Scripts\\python.exe vision\\body_tracker.py --ip 192.168.4.1
  .\\.venv-camera\\Scripts\\python.exe vision\\body_tracker.py --ip 192.168.4.1 --dry-run
  .\\.venv-camera\\Scripts\\python.exe vision\\body_tracker.py --source webcam:0 --dry-run
  .\\.venv-camera\\Scripts\\python.exe vision\\body_tracker.py --source photos\\ --dry-run --fps 1

Window keys: q = quit, p = pause/resume sending.
"""
import argparse
from pathlib import Path
import sys
import time

import cv2
import numpy as np
import requests

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "camera"))
from body_protocol import Person, frame_message  # noqa: E402
from protocol import serial_control_url, ultralytics_model_path  # noqa: E402

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp"}


class CameraSource:
    """Still images from the OhStem camera's /capture (stock firmware)."""

    def __init__(self, base, session, timeout):
        self.base, self.session, self.timeout, self.errors = base, session, timeout, 0

    def read(self):
        try:
            reply = self.session.get(self.base + "/capture", params={"_cb": time.time_ns()},
                                     timeout=(2, self.timeout))
            reply.raise_for_status()
            frame = cv2.imdecode(np.frombuffer(reply.content, np.uint8), cv2.IMREAD_COLOR)
            if frame is None:
                raise ValueError("/capture did not return an image")
        except (requests.RequestException, ValueError) as exc:
            self.errors += 1
            retry = min(3.0, 0.5 * 2 ** min(self.errors, 3))
            print(f"[Camera ERROR #{self.errors}] retry in {retry:g}s: {exc}")
            time.sleep(retry)
            return None
        if self.errors:
            print(f"[Camera] Recovered after {self.errors} failed requests")
            self.errors = 0
        return frame


class WebcamSource:
    def __init__(self, index):
        self.cap = cv2.VideoCapture(index)
        if not self.cap.isOpened():
            raise RuntimeError(f"Cannot open webcam {index}")

    def read(self):
        ok, frame = self.cap.read()
        return frame if ok else None


class ImageSource:
    def __init__(self, path):
        path = Path(path)
        files = sorted(p for p in path.iterdir() if p.suffix.lower() in IMAGE_SUFFIXES) \
            if path.is_dir() else [path]
        self.frames = [(p.name, cv2.imread(str(p))) for p in files]
        self.frames = [(n, f) for n, f in self.frames if f is not None]
        if not self.frames:
            raise RuntimeError(f"No readable images in {path}")
        self.index = 0

    def read(self):
        name, frame = self.frames[self.index % len(self.frames)]
        self.index += 1
        print(f"[Image] {name}")
        return frame.copy()


class YoloPeople:
    """YOLO person detector (class 0) on the laptop."""

    def __init__(self, model_path, confidence):
        from ultralytics import YOLO
        self.model = YOLO(ultralytics_model_path(model_path))
        self.confidence = confidence

    def detect(self, frame):
        people = []
        for result in self.model(frame, classes=[0], conf=self.confidence, verbose=False):
            for box in result.boxes:
                x1, y1, x2, y2 = (float(v) for v in box.xyxy[0])
                people.append(Person(x1, y1, x2, y2, float(box.conf[0])))
        return people


def make_source(args, base, session):
    if args.source == "camera":
        return CameraSource(base, session, args.capture_timeout)
    if args.source.startswith("webcam:"):
        return WebcamSource(int(args.source.split(":", 1)[1]))
    return ImageSource(args.source)


def draw(frame, people, chosen, label, sending, margin_frac):
    h, w = frame.shape[:2]
    m = int(round(margin_frac * min(w, h)))
    cv2.rectangle(frame, (m, m), (w - m, h - m), (255, 200, 0), 1)  # UNO's safe area
    for p in people:
        colour = (0, 255, 0) if p is chosen else (0, 165, 255)
        cv2.rectangle(frame, (int(p.x1), int(p.y1)), (int(p.x2), int(p.y2)), colour, 2)
    cv2.putText(frame, f"{label}  [{'SEND' if sending else 'NOT SENDING'}]", (10, 25),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ip", default="192.168.4.1", help="Camera IP (default: camera Wi-Fi AP)")
    parser.add_argument("--source", default="camera", help="camera | webcam:<index> | <image or folder>")
    parser.add_argument("--model", type=Path, default=ROOT / "yolo26n.pt")
    parser.add_argument("--conf", type=float, default=0.5, help="YOLO confidence threshold")
    parser.add_argument("--fps", type=float, default=5, help="Max frames/messages per second (<= 5)")
    parser.add_argument("--multi-ratio", type=float, default=0.5,
                        help="2nd person at least this fraction of the 1st box area -> MULTI")
    parser.add_argument("--min-area-px", type=int, default=0, help="Ignore smaller boxes (people far away)")
    parser.add_argument("--margin", type=float, default=0.03,
                        help="Only for the preview: safe border the UNO uses (fraction)")
    parser.add_argument("--dry-run", action="store_true", help="Only print messages, send nothing")
    parser.add_argument("--no-window", action="store_true")
    parser.add_argument("--capture-timeout", type=float, default=4)
    parser.add_argument("--control-timeout", type=float, default=1)
    args = parser.parse_args()
    if not 0 < args.fps <= 5:
        parser.error("--fps must be in (0, 5]")

    base = f"http://{args.ip}"
    session = requests.Session()
    try:
        source = make_source(args, base, session)
        detector = YoloPeople(args.model, args.conf)
    except (RuntimeError, FileNotFoundError) as exc:
        sys.exit(f"[ERROR] {exc}")

    print(f"[Body] source={args.source} fps<={args.fps:g} "
          f"{'DRY RUN (not sending)' if args.dry_run else 'sending to ' + base + '/control'}")
    sending = not args.dry_run
    last_label, last_print, next_frame = None, 0.0, 0.0
    delays, last_timing = [], time.monotonic()
    try:
        while True:
            wait = next_frame - time.monotonic()
            if wait > 0:
                time.sleep(wait)
            next_frame = time.monotonic() + 1 / args.fps
            t_request = time.monotonic()
            frame = source.read()
            if frame is None:
                continue
            t_frame = time.monotonic()
            people = detector.detect(frame)
            h, w = frame.shape[:2]
            label, confidence, chosen = frame_message(people, w, h, args.multi_ratio, args.min_area_px)
            t_detect = time.monotonic()
            if sending:
                try:
                    session.get(serial_control_url(base, label, confidence), timeout=args.control_timeout)
                except requests.RequestException as exc:
                    print(f"[Control ERROR] {exc}")
            now = time.monotonic()
            delays.append((t_frame - t_request, t_detect - t_frame, now - t_detect))
            if label != last_label or now - last_print >= 2:
                print(f"[Body] {label}  ({len(people)} person(s))")
                last_label, last_print = label, now
            if now - last_timing >= 10 and len(delays) >= 5:
                cap, det, ctl = (sorted(d[i] for d in delays)[int(0.9 * len(delays))] * 1000 for i in range(3))
                total = cap + det + ctl
                print(f"[Timing] 90% of frames: capture {cap:.0f} + detect {det:.0f} + control {ctl:.0f} "
                      f"= {total:.0f} ms -> set servo_settle >= {int(total + 200)} "
                      f"(usb_viewer.py --set servo_settle=...)")
                delays.clear()
                last_timing = now
            if not args.no_window:
                draw(frame, people, chosen, label, sending, args.margin)
                cv2.imshow("Body tracker (q quit, p pause)", frame)
                key = cv2.waitKey(1) & 0xFF
                if key == ord("q"):
                    break
                if key == ord("p") and not args.dry_run:
                    sending = not sending
                    print(f"[Body] {'Sending' if sending else 'Paused'}")
    except KeyboardInterrupt:
        pass
    finally:
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
