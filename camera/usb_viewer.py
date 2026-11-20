"""View JPEGs/results from YOLO UNO. No model or inference runs on this PC.

  usb_viewer.py --port COM9                      xem anh + ket qua
  usb_viewer.py --port COM9 --configure          nhap Wi-Fi, camera, MAC can, lap dat, servo
  usb_viewer.py --port COM5 --preview            test camera + YOLO tren UNO, khong can can/servo
  usb_viewer.py --port COM5 --preview --log .edge-deps/qa/camera.jsonl  ghi QA metadata, khong luu anh
  usb_viewer.py --port COM9 --set servo_pin=10 --set hold_sec=12   gui cau hinh khong can hoi

Frame JSON fields drawn when present (older firmware may omit any of them):
status, boxes, height_cm, distance_m, weight_kg, weight_stable, bmi, processing_ms,
phase, servo_deg, tilt_deg, framing, margin_px (Plan 2).
"""
import argparse
import getpass
import math
import time

from usb_protocol import SERVO_LIMITS, Parser, SessionFilter, config_commands
from telemetry import POWER_KEYS, PacketRecorder, power_lines

# ---------------------------------------------------------------------------
# Pure helpers (no OpenCV): tested by tests/test_usb_viewer.py
# ---------------------------------------------------------------------------

YELLOW, GREEN, ORANGE, RED, WHITE = (0, 255, 255), (0, 220, 0), (0, 165, 255), (0, 0, 255), (255, 255, 255)

# framing (Plan 2 FramingStatus) -> (box colour, hint shown to the operator)
FRAMING_STYLE = {
    "fullbody": (GREEN, ""),
    "topclipped": (ORANGE, "Head cut off - servo should tilt up"),
    "bottomclipped": (ORANGE, "Feet cut off - servo should tilt down"),
    "sideclipped": (ORANGE, "Person cut at the side"),
    "tooclose": (RED, "Step back! Too close to camera"),
    "none": (YELLOW, "No person in frame"),
}


def _number(value):
    """Finite float or None (bool, text and NaN are not numbers here)."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    value = float(value)
    return value if math.isfinite(value) else None


def fmt(value, digits=1, unit=""):
    number = _number(value)
    return "--" if number is None else f"{number:.{digits}f}{unit}"


def safe_boxes(raw):
    """[(x1, y1, x2, y2, conf)] from metadata['boxes'], skipping malformed entries."""
    if not isinstance(raw, (list, tuple)):
        return []
    out = []
    for box in raw:
        if not isinstance(box, (list, tuple)) or len(box) < 4:
            continue
        values = [_number(v) for v in box[:5]]
        if any(v is None for v in values[:4]):
            continue
        x1, y1, x2, y2 = values[:4]
        if x2 <= x1 or y2 <= y1:
            continue
        conf = values[4] if len(values) > 4 and values[4] is not None else 0.0
        out.append((x1, y1, x2, y2, conf))
    return out


def framing_style(framing):
    """(colour, hint) for a framing string; unknown values stay visible as text."""
    if not isinstance(framing, str) or not framing:
        return GREEN, ""
    return FRAMING_STYLE.get(framing.replace("_", "").lower(), (YELLOW, ""))


def margin_rect(width, height, margin_px):
    """Safe-area rectangle (x1, y1, x2, y2) or None when margin_px is missing/invalid."""
    m = _number(margin_px)
    if m is None or m <= 0 or 2 * m >= min(width, height):
        return None
    return (round(m), round(m), round(width - m), round(height - m))


def overlay_lines(metadata):
    """[(text, colour)] for the top-left overlay of a frame."""
    if metadata.get("preview") is True:
        return [("Camera test - scale and servo disabled", WHITE),
                (str(metadata.get("status") or "Waiting for camera"), YELLOW),
                (f"On-board processing: {fmt(metadata.get('processing_ms'), 0, ' ms')}", YELLOW),
                (f"Camera fetch over WiFi: {fmt(metadata.get('capture_ms'), 0, ' ms')}", WHITE)]
    lines = [(str(metadata.get("status") or ""), YELLOW),
             (f"Height: {fmt(metadata.get('height_cm'), 1, ' cm')}   "
              f"Distance: {fmt(metadata.get('distance_m'), 2, ' m')}", YELLOW),
             (f"Weight: {fmt(metadata.get('weight_kg'), 2, ' kg')} "
              f"{'LOCK' if metadata.get('weight_stable') is True else 'moving'}   "
              f"BMI: {fmt(metadata.get('bmi'), 1)}", YELLOW)]
    plan2 = [k for k in ("phase", "servo_deg", "tilt_deg", "framing") if k in metadata]
    if plan2:
        phase = metadata.get("phase")
        framing = metadata.get("framing")
        colour, hint = framing_style(framing)
        lines.append((f"Phase: {phase if isinstance(phase, str) and phase else '--'}   "
                      f"Servo: {fmt(metadata.get('servo_deg'), 1, ' deg')}   "
                      f"Tilt: {fmt(metadata.get('tilt_deg'), 1, ' deg')}", WHITE))
        if isinstance(framing, str) and framing:
            lines.append((f"Framing: {framing}" + (f"  - {hint}" if hint else ""), colour))
    lines.append((f"On-board processing: {fmt(metadata.get('processing_ms'), 0, ' ms')}", YELLOW))
    return lines


def status_text(metadata):
    """Text for a 'status' packet (no image), including the phase when sent."""
    message = metadata.get("message")
    message = message if isinstance(message, str) and message else "Waiting"
    phase = metadata.get("phase")
    return f"[{phase}] {message}" if isinstance(phase, str) and phase else message


def parse_set_args(items):
    """['servo_pin=10', 'hold_sec=12'] -> {'servo_pin': '10', ...} (validated later)."""
    values = {}
    for item in items or []:
        key, sep, value = item.partition("=")
        if not sep or not key.strip():
            raise ValueError(f"--set expects key=value, got {item!r}")
        values[key.strip()] = value.strip()
    return values


# ---------------------------------------------------------------------------
# Interactive configuration
# ---------------------------------------------------------------------------

SERVO_PROMPTS = [
    ("servo_pin", "Chan GPIO servo (Plan 2 de xuat 10)"),
    ("servo_bottom", "Goc lenh servo diem bat dau quet (do)"),
    ("servo_top", "Goc lenh servo diem ket thuc quet (do)"),
    ("servo_step", "Buoc quet moi lan (do)"),
    ("servo_level", "Goc lenh servo khi camera nam ngang (do)"),
    ("servo_dir", "Chieu: 1 hoac -1 (dau cua goc nghieng so voi goc lenh)"),
    ("servo_settle", "Thoi gian cho sau moi lan quay (ms)"),
    ("hold_sec", "Giu ket qua H/W/BMI sau khi buoc xuong (giay)"),
]


def ask_number(prompt, default, low, high, zero=False):
    while True:
        text = input(f"{prompt} [{default}]: ").strip() or str(default)
        try:
            value = float(text)
            if low <= value <= high or (zero and value == 0):
                return value
        except ValueError:
            pass
        print(f"Nhap so trong khoang {low}..{high}.")


def ask_yes(prompt, default):
    text = input(f"{prompt} [{'Y/n' if default else 'y/N'}]: ").strip().lower()
    return default if not text else text in {"y", "yes", "c", "co"}


def ask_optional(key, prompt):
    """Ask one Plan 2 key; Enter keeps the value stored on the UNO (returns None)."""
    _, low, high = SERVO_LIMITS[key]
    while True:
        text = input(f"{prompt} [{low}..{high}, Enter = giu nguyen]: ").strip()
        if not text:
            return None
        try:
            config_commands({key: text})  # Same validation as the sent command.
            return text
        except ValueError as error:
            print(f"Khong hop le: {error}")


def ask_mac():
    while True:
        text = input("MAC can Mi Scale 2 (scaleMac), vi du 70:87:9e:ea:3a:e0 (Enter = giu nguyen): ").strip()
        if not text:
            return None
        try:
            config_commands({"scaleMac": text})
            return text
        except ValueError as error:
            print(f"Khong hop le: {error}")


def ask_geometry(values):
    mode = input("Che do fixed / ground / servo (servo = Plan 2, env yolo_uno_servo_sweep) [fixed]: "
                 ).strip().lower() or "fixed"
    if mode not in {"fixed", "ground", "servo"}:
        raise ValueError("Chi chap nhan fixed, ground hoac servo")
    print("fy/H = tieu cu pixel chia chieu cao anh; KHONG nhap tieu cu mm.")
    if mode == "servo":
        # Plan 2: the tilt comes from the servo (servo_level/servo_dir), so only the
        # mounting height and fy/H are needed; no "mode" key (the firmware has one mode).
        values["height"] = ask_number("Truc servo/tam camera cao hon mat can (cm)", 110, 1, 400)
        values["focal"] = ask_number("fy/H (OV2640 3.6 mm o 320x240 ~ 1.33)", 1.33, .01, 20)
        return
    values["mode"] = mode
    if mode == "fixed":
        print("Camera dat ngang. Nguoi chuan phai dung trong khung ngay frame full-body dau tien.")
        values["distance"] = ask_number("Khoang cach camera - vi tri dung (m)", 3, .1, 20)
        values["reference"] = ask_number("Chieu cao that nguoi hieu chuan (cm)", 183, 30, 250)
        values["focal"] = ask_number("fy/H (0 = hieu chuan lai bang nguoi chuan)", 0, .01, 20, zero=True)
        values["tilt"] = 0
    else:
        values["height"] = ask_number("Tam camera cao hon mat can (cm)", 100, 1, 400)
        values["tilt"] = ask_number("Goc nghieng xuong (do, duong = xuong)", 0, -79, 79)
        values["focal"] = ask_number("fy/H (lay tu hieu chuan camera)", 1, .01, 20)


def configure():
    """Ask section by section; returns the command bytes, or None if nothing changed."""
    values = {}
    ssid = input("WiFi SSID (Enter = giu nguyen): ").strip()
    if ssid:
        values["ssid"] = ssid
        values["password"] = getpass.getpass("WiFi password (khong hien / khong ghi file): ")
    url = input("URL anh camera (Enter = giu nguyen, vi du http://10.127.7.68/capture): ").strip()
    if url:
        if not url.startswith("http://"):
            raise ValueError("Camera URL phai bat dau bang http://")
        values["camera"] = url
    mac = ask_mac()
    if mac:
        values["scaleMac"] = mac
    # Default yes: same questions as before for users who just press Enter.
    if ask_yes("Nhap thong so lap dat (mode, khoang cach, fy/H)?", True):
        ask_geometry(values)
    if ask_yes("Nhap thong so servo va thoi gian giu ket qua (Plan 2)?", False):
        while True:
            servo = {key: ask_optional(key, prompt) for key, prompt in SERVO_PROMPTS}
            try:
                config_commands(servo)  # Cross-checks, e.g. bottom != top.
                values.update(servo)
                break
            except ValueError as error:
                print(f"Khong hop le: {error}. Nhap lai phan servo.")
    values = {k: v for k, v in values.items() if v is not None}
    if not values:
        print("Khong co gi thay doi, khong gui cau hinh.")
        return None
    return config_commands(values)


# ---------------------------------------------------------------------------
# Drawing + main loop (OpenCV)
# ---------------------------------------------------------------------------

def render_frame(cv2, image, metadata, width=800):
    """Draw boxes, safe area and text; returns the resized canvas."""
    h, w = image.shape[:2]
    colour, _ = framing_style(metadata.get("framing"))
    rect = margin_rect(w, h, metadata.get("margin_px"))
    if rect:
        cv2.rectangle(image, rect[:2], rect[2:], (255, 200, 0), 1)
    for x1, y1, x2, y2, _ in safe_boxes(metadata.get("boxes")):
        cv2.rectangle(image, (round(x1), round(y1)), (round(x2), round(y2)), colour, 2)
    canvas = cv2.resize(image, (width, round(h * width / w)))
    lines = overlay_lines(metadata)
    if POWER_KEYS.intersection(metadata):
        lines += [(line, WHITE) for line in power_lines(metadata)]
    for i, (line, line_colour) in enumerate(lines):
        cv2.putText(canvas, line, (10, 25 + 25 * i), cv2.FONT_HERSHEY_SIMPLEX, .55, line_colour, 2)
    return canvas


def scale_notice_lines(metadata):
    """TV2 power packets do not imply camera/height/BMI validity."""
    lines = [f"{metadata.get('phase') or 'Waiting'} | CPU: {fmt(metadata.get('cpu_mhz'), 0, ' MHz')}"
             f" | BLE: {fmt(metadata.get('ble_duty_percent'), 1, '%')}",
             f"Weight: {fmt(metadata.get('weight_kg'), 2, ' kg')} | "
             f"{'LOCK' if metadata.get('weight_stable') is True else 'unlocked' if metadata.get('weight_stable') is False else 'LOCK unknown'}"]
    if metadata.get("phase") == "RESULT_HOLD":
        lines.append(f"Height: {fmt(metadata.get('height_cm'), 1, ' cm')} | "
                     f"BMI: {fmt(metadata.get('bmi'), 1)}")
    return lines + power_lines(metadata)


def _run():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", required=True, help="Vi du COM9")
    parser.add_argument("--configure", action="store_true", help="Nhap WiFi, camera, MAC can, lap dat, servo")
    parser.add_argument("--preview", action="store_true", help="Test anh + YOLO tren UNO, khong can can/servo")
    parser.add_argument("--log", help="Append metadata-only QA JSONL (no JPEG/password); use .edge-deps/qa/")
    parser.add_argument("--set", action="append", metavar="KEY=VALUE",
                        help="Gui mot khoa cau hinh, lap lai duoc (vi du --set hold_sec=12)")
    args = parser.parse_args()
    try:
        preset = config_commands(parse_set_args(args.set)) if args.set else None
    except ValueError as error:
        parser.error(str(error))  # Before opening the port: nothing is sent.
    import cv2
    import numpy as np
    decoder = Parser()
    sessions = SessionFilter()
    title = "SmartScale - YOLO UNO USB viewer (Q to quit)"
    background = np.zeros((480, 800, 3), np.uint8)
    last_image = 0
    first_frame = True
    first_person = True
    notice = "Waiting for YOLO UNO..."
    notice_lines = [notice]
    config_warning = None
    with PacketRecorder(args.log) as recorder, open_device(args.port) as port:
        print(f"Da mo {args.port}.", flush=True)
        commands = configure() if args.configure else preset
        if args.configure and preset:
            print("Bo qua --set vi da dung --configure.")
        # Do not toggle DTR/RTS: this board uses native USB Serial/JTAG.
        if commands:
            port.write(commands)
        if args.preview:
            print("Camera test: YOLO runs on UNO; scale/servo disabled. Q to stop.", flush=True)
        renew_at = 0
        try:
            while True:
                if args.preview and time.monotonic() >= renew_at:
                    port.write(b"PREVIEW ON\n")
                    renew_at = time.monotonic() + 3
                frames = decoder.feed(port.read(min(max(port.in_waiting, 1), 65536)))
                for metadata, jpeg in frames:
                    recorder.write(metadata, len(jpeg))
                    previous_session = sessions.session
                    if not sessions.accept(metadata):
                        continue
                    if previous_session is not None and sessions.session != previous_session:
                        last_image = 0  # Never display the previous person's image during a new arrival.
                        notice_lines = ["New scale session. Waiting for a fresh frame..."]
                    kind = metadata.get("kind")
                    if kind == "config":
                        print("Da luu cau hinh." if metadata.get("ok") else "Cau hinh bi tu choi.")
                        config_warning = None if metadata.get("ok") else "Config rejected - check terminal and retry"
                        last_image = 0
                        notice_lines = ["Configuration saved. Waiting for a fresh frame." if metadata.get("ok")
                                        else "Configuration rejected. Check terminal values and retry."]
                    elif kind == "boot":
                        print("On-board self-test:", metadata)
                        last_image = 0
                        notice_lines = ["UNO restarted. Waiting for camera..."]
                    elif kind == "scale" and not args.preview:
                        notice_lines = scale_notice_lines(metadata)
                        if metadata.get("phase") in {"STANDBY", "DRAINING", "RESULT_HOLD"}:
                            last_image = 0
                    elif kind == "status":
                        notice = status_text(metadata)
                        notice_lines = [notice]
                        if metadata.get("preview") is not True and not args.preview:
                            last_image = 0
                    elif kind == "frame" and jpeg:
                        image = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
                        if image is None:
                            continue
                        if first_frame:
                            print(f"Da nhan anh {image.shape[1]}x{image.shape[0]} tu UNO; "
                                  f"YOLO: {metadata.get('processing_ms', '?')} ms.", flush=True)
                            first_frame = False
                        if first_person and safe_boxes(metadata.get("boxes")):
                            print("Da nhan dien nguoi tren YOLO UNO.", flush=True)
                            first_person = False
                        background = render_frame(cv2, image, metadata)
                        last_image = time.monotonic()
                if not last_image or time.monotonic() - last_image > 15:
                    screen = np.zeros((480, 800, 3), np.uint8)
                    lines = notice_lines if not last_image else ["No fresh frame from YOLO UNO"]
                    for row, line in enumerate(lines):
                        cv2.putText(screen, line, (12, 50 + 28 * row),
                                    cv2.FONT_HERSHEY_SIMPLEX, .65, (0, 255, 255), 2)
                else:
                    screen = background
                if config_warning:
                    screen = screen.copy()
                    cv2.putText(screen, config_warning, (12, screen.shape[0] - 18),
                                cv2.FONT_HERSHEY_SIMPLEX, .55, RED, 2)
                cv2.imshow(title, screen)
                if cv2.waitKey(1) & 0xff in (ord('q'), 27):
                    break
        finally:
            if args.preview:
                try:
                    port.write(b"PREVIEW OFF\n")
                    port.flush()
                except OSError:
                    pass  # The firmware lease expires if USB was disconnected.
            cv2.destroyAllWindows()


def open_device(name):
    import serial
    from serial.tools import list_ports
    try:
        return serial.Serial(name, 115200, timeout=.05, write_timeout=3)
    except serial.SerialException as error:
        print(f"Khong mo duoc cong {name}: {error}", flush=True)
        ports = list(list_ports.comports())
        print("Cac cong Windows dang nhan:", flush=True)
        for port in ports:
            print(f"  {port.device}: {port.description}", flush=True)
        if not ports:
            print("  Khong co cong nao.", flush=True)
        print("Kiem tra cap USB, cam lai UNO va dong Serial Monitor/viewer khac.", flush=True)
        print("Neu so COM thay doi, chay lai voi --port COM_moi. Chua gui cau hinh.", flush=True)
        raise SystemExit(1) from None


def main():
    try:
        return _run()
    except OSError as error:
        print(f"USB/log I/O error: {error}", flush=True)
        print("Da dong viewer. Kiem tra USB/so COM, dong monitor khac va chay lai lenh.", flush=True)
        print("Cau hinh khong tu gui lai; preview het han sau 10 s neu mat USB.", flush=True)
        return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("Da dong USB viewer.")
