"""View JPEGs/results from YOLO UNO. No model or inference runs on this PC."""
import argparse
import math
import re
import time

from usb_protocol import Parser, SessionFilter, config_commands

_MAC_RE = re.compile(r"^[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}$")
_RESERVED_SERVO_PINS = {11, 12, 19, 20, 43, 44, 45, 46} | set(range(22, 38))


def validate_mac(text):
    stripped = text.strip()
    if not stripped:
        return ""
    if not _MAC_RE.fullmatch(stripped):
        raise ValueError("Dia chi MAC phai co dang AA:BB:CC:DD:EE:FF (6 cap hex).")
    return stripped.lower()


def validate_hold_sec(text, default=10):
    stripped = text.strip()
    if not stripped:
        return default
    if not re.fullmatch(r"[0-9]+", stripped):
        raise ValueError("Thoi gian giu ket qua phai la so nguyen tu 0 den 60 giay.")
    value = int(stripped)
    if not 0 <= value <= 60:
        raise ValueError("Thoi gian giu ket qua phai la so nguyen tu 0 den 60 giay.")
    return value


def validate_servo_pin(text, default=None):
    stripped = text.strip()
    if not stripped:
        return default
    if not re.fullmatch(r"-?[0-9]+", stripped):
        raise ValueError("Chan servo phai la -1 (tat) hoac so GPIO hop le (0..48).")
    pin = int(stripped)
    if pin == -1:
        return -1
    if not 0 <= pin <= 48 or pin in _RESERVED_SERVO_PINS:
        raise ValueError("GPIO servo khong hop le hoac trung I2C/USB/Flash/UART.")
    return pin


def validate_servo_dir(text, default=1):
    stripped = text.strip()
    if not stripped:
        return default
    if stripped in {"1", "+1"}:
        return 1
    if stripped == "-1":
        return -1
    raise ValueError("Chieu servo_dir chi nhan 1 hoac -1.")


def validate_servo_settle(text, default=200):
    stripped = text.strip()
    if not stripped:
        return default
    if not re.fullmatch(r"[0-9]+", stripped):
        raise ValueError("Thoi gian cho het rung servo_settle phai tu 50 den 2000 ms.")
    value = int(stripped)
    if not 50 <= value <= 2000:
        raise ValueError("Thoi gian cho het rung servo_settle phai tu 50 den 2000 ms.")
    return value


def _is_finite_number(value):
    return (value is not None and not isinstance(value, bool)
            and isinstance(value, (int, float)) and math.isfinite(value))


def format_servo_line(metadata):
    if not any(k in metadata for k in ("phase", "servo_deg", "tilt_deg", "framing")):
        return None
    parts = []
    phase = metadata.get("phase")
    if phase:
        parts.append(f"Phase: {phase}")
    servo_deg = metadata.get("servo_deg")
    tilt_deg = metadata.get("tilt_deg")
    if _is_finite_number(servo_deg):
        off_tag = " [OFF]" if metadata.get("servo_attached") is False else ""
        if _is_finite_number(tilt_deg):
            parts.append(f"Servo: {servo_deg:.1f} deg{off_tag} (tilt {tilt_deg:+.1f} deg)")
        else:
            parts.append(f"Servo: {servo_deg:.1f} deg{off_tag}")
    elif _is_finite_number(tilt_deg):
        parts.append(f"Tilt: {tilt_deg:+.1f} deg")
    framing = metadata.get("framing")
    if framing:
        parts.append(f"Frame: {framing}")
    return " | ".join(parts) if parts else None


def framing_box_color(framing):
    if framing == "TOO_CLOSE":
        return (0, 0, 255)
    if framing in {"TOP_CLIPPED", "BOTTOM_CLIPPED", "SIDE_CLIPPED"}:
        return (0, 165, 255)
    return (0, 255, 0)


def format_metric(value, unit="", fmt=".1f"):
    if value is None or isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return "--"
    formatted = format(value, fmt)
    return f"{formatted} {unit}" if unit else formatted


def format_timing_line(metadata):
    proc_ms = metadata.get("processing_ms")
    cap_ms = metadata.get("capture_ms")
    if cap_ms is not None:
        keep_alive = metadata.get("keep_alive")
        ka_tag = ""
        if isinstance(keep_alive, bool):
            ka_tag = " [KA]" if keep_alive else " [NEW]"
        line = f"AI: {proc_ms} ms   Net: {cap_ms} ms{ka_tag}"
    else:
        line = f"On-board processing: {proc_ms} ms"
    rssi = metadata.get("wifi_rssi")
    if isinstance(rssi, int) and not isinstance(rssi, bool) and rssi != 0:
        line += f"   RSSI: {rssi} dBm"
    return line


def open_device(name):
    import serial
    from serial.tools import list_ports
    try:
        # Disable DTR/RTS before opening so Windows does not pulse the
        # ESP32-S3 USB-Serial/JTAG reset circuit on open/close.
        port = serial.Serial(None, 115200, timeout=.01, write_timeout=3, dsrdtr=False, rtscts=False)
        port.port = name
        port.dtr = False
        port.rts = False
        port.open()
        port.reset_input_buffer()
        return port
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


def ask_number(prompt, default, low, high, zero=False):
    while True:
        text = input(f"{prompt} [{default}]: ").strip() or str(default)
        try:
            value = float(text)
            if math.isfinite(value) and (low <= value <= high or (zero and value == 0)):
                return value
        except ValueError:
            pass
        print(f"Nhap so trong khoang {low}..{high}.")


def configure():
    values = {}
    while True:
        ssid = input("WiFi SSID (Enter = giu nguyen): ").strip()
        if not ssid:
            break
        if len(ssid.encode()) > 32:
            print("WiFi SSID toi da 32 byte.")
            continue
        values["ssid"] = ssid
        while True:
            password = input("WiFi password (hien khi nhap, khong ghi file, Enter neu WiFi mo): ")
            if "\r" in password or "\n" in password or "\x00" in password:
                print("Mat khau khong duoc chua ky tu xuong dong.")
                continue
            if len(password.encode()) > 64:
                print("WiFi password toi da 64 byte.")
                continue
            values["password"] = password
            break
        break

    while True:
        url = input("URL hoac IP camera (Enter = giu nguyen, vi du http://10.127.7.68/capture): ").strip()
        if not url:
            break
        if not url.startswith(("http://", "https://")):
            url = "http://" + url
        if not url.startswith("http://"):
            print("Camera URL phai bat dau bang http:// (camera OhStem khong ho tro HTTPS).")
            continue
        host_and_path = url[len("http://"):].rstrip("/")
        if not host_and_path:
            print("URL/IP camera khong hop le.")
            continue
        if "/" not in host_and_path:
            url = f"http://{host_and_path}/capture"
            print(f"  -> Tu dong them /capture: {url}")
        if len(url.encode()) > 159:
            print("Camera URL qua dai (toi da 159 byte).")
            continue
        values["camera"] = url
        break

    while True:
        mac_raw = input("MAC can Mi Scale 2 (Enter = giu nguyen, vd 70:87:9e:ea:3a:e0, 00:00:00:00:00:00 = tat loc): ")
        try:
            mac = validate_mac(mac_raw)
            if mac:
                values["mac"] = mac
            break
        except ValueError as exc:
            print(exc)

    while True:
        servo_raw = input("GPIO servo (Enter = giu nguyen, 10 = D7, -1 = tat servo): ")
        try:
            servo_pin = validate_servo_pin(servo_raw, default=None)
            break
        except ValueError as exc:
            print(exc)

    if servo_pin is not None:
        values["servo_pin"] = servo_pin
        if servo_pin != -1:
            servo_min = ask_number("Gioi han duoi co khi servo_min (do)", 45, 0, 179)
            servo_max = ask_number("Gioi han tren co khi servo_max (do)", max(servo_min + 1, 135), servo_min + 1, 180)
            values["servo_min"] = servo_min
            values["servo_max"] = servo_max
            values["servo_level"] = ask_number("Goc servo nam ngang servo_level (do)", 90, 0, 180)
            while True:
                dir_raw = input("Chieu quay servo_dir (+1 = tang goc chui xuong, -1 = nguoc lai) [1]: ")
                try:
                    servo_dir = validate_servo_dir(dir_raw, default=1)
                    values["servo_dir"] = servo_dir
                    break
                except ValueError as exc:
                    print(exc)
            default_bottom = 112 if servo_dir == 1 else 68
            default_top = 85 if servo_dir == 1 else 95
            default_bottom = min(max(default_bottom, servo_min), servo_max)
            default_top = min(max(default_top, servo_min), servo_max)
            values["servo_bottom"] = ask_number("Goc bat dau quet phia duoi servo_bottom (do)", default_bottom, servo_min, servo_max)
            values["servo_top"] = ask_number("Goc quet cao nhat servo_top (do)", default_top, servo_min, servo_max)
            values["servo_step"] = ask_number("Buoc quet servo_step (do)", 4.5, 1, 30)
            while True:
                settle_raw = input("Thoi gian cho het rung servo_settle (ms, 50..2000) [200]: ")
                try:
                    values["servo_settle"] = validate_servo_settle(settle_raw, default=200)
                    break
                except ValueError as exc:
                    print(exc)

    default_mode = "ground" if (servo_pin is not None and servo_pin != -1) else "fixed"
    while True:
        mode = input(f"Che do fixed / ground [{default_mode}]: ").strip().lower() or default_mode
        if mode in {"fixed", "ground"}:
            values["mode"] = mode
            break
        print("Chi chap nhan fixed hoac ground.")

    print("fy/H = tieu cu pixel chia chieu cao anh; KHONG nhap tieu cu mm.")
    if mode == "fixed":
        print("Camera dat ngang. Nguoi chuan phai dung trong khung ngay frame full-body dau tien.")
        values["distance"] = ask_number("Khoang cach camera - vi tri dung (m)", 3, .1, 20)
        values["reference"] = ask_number("Chieu cao that nguoi hieu chuan (cm)", 183, 30, 250)
        values["focal"] = ask_number("fy/H (0 = hieu chuan lai bang nguoi chuan)", 0, .01, 20, zero=True)
        values["tilt"] = 0
    else:
        values["height"] = ask_number("Tam camera cao hon mat can (cm)", 100, 1, 400)
        values["tilt"] = ask_number("Goc nghieng bu tilt_offset (do, 0 neu da chuan servo_level)", 0, -79, 79)
        values["focal"] = ask_number("fy/H (lay tu hieu chuan camera)", 1, .01, 20)

    while True:
        hold_raw = input("Thoi gian giu ket qua LCD sau khi buoc xuong (giay, 0..60, 0 = tat) [10]: ")
        try:
            values["hold_sec"] = validate_hold_sec(hold_raw, default=10)
            break
        except ValueError as exc:
            print(exc)
    return config_commands(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Vi du COM9")
    parser.add_argument("--configure", action="store_true", help="Nhap WiFi va thong so lap dat")
    args = parser.parse_args()
    import cv2
    import numpy as np
    import serial
    decoder = Parser()
    sessions = SessionFilter()
    title = "SmartScale - YOLO UNO USB viewer (Q to quit)"
    background = np.zeros((480, 800, 3), np.uint8)
    last_image = 0
    notice = "Waiting for YOLO UNO..."
    try:
        with open_device(args.port) as port:
            print(f"Da mo {args.port}.", flush=True)
            commands = configure() if args.configure else None
            awaiting_config_ack_deadline = None
            # Do not toggle DTR/RTS: this board uses native USB Serial/JTAG.
            if commands:
                port.reset_input_buffer()
                port.write(commands)
                port.flush()
                awaiting_config_ack_deadline = time.monotonic() + 5.0
            while True:
                try:
                    frames = []
                    drained = 0
                    while port.in_waiting > 0 and drained < 262144:
                        chunk = port.read(min(port.in_waiting, 65536))
                        if not chunk:
                            break
                        drained += len(chunk)
                        frames.extend(decoder.feed(chunk))
                except serial.SerialException as exc:
                    print(f"Mat ket noi USB voi YOLO UNO: {exc}", flush=True)
                    break

                latest_frame = None
                for metadata, jpeg in frames:
                    if not sessions.accept(metadata):
                        continue
                    kind = metadata.get("kind")
                    if kind == "config":
                        awaiting_config_ack_deadline = None
                        print("Da luu cau hinh." if metadata.get("ok") else "Cau hinh bi tu choi.")
                        if metadata.get("ok") and not last_image:
                            notice = "Configuration saved. Fetching camera frame..."
                    elif kind == "boot":
                        print("On-board self-test:", metadata)
                    elif kind == "scale":
                        phase = metadata.get("phase", "Waiting")
                        notice = (f"{phase} | CPU: {metadata.get('cpu_mhz')} MHz | "
                                  f"BLE: {metadata.get('ble_duty_percent')}%")
                        servo_deg = metadata.get("servo_deg")
                        tilt_deg = metadata.get("tilt_deg")
                        if _is_finite_number(servo_deg):
                            if _is_finite_number(tilt_deg):
                                notice += f" | Servo: {servo_deg:.1f} deg (tilt {tilt_deg:+.1f} deg)"
                            else:
                                notice += f" | Servo: {servo_deg:.1f} deg"
                        if phase == "RESULT_HOLD":
                            notice += (f" | H: {format_metric(metadata.get('height_cm'), 'cm', '.1f')}  "
                                       f"W: {format_metric(metadata.get('weight_kg'), 'kg', '.2f')}  "
                                       f"BMI: {format_metric(metadata.get('bmi'), '', '.2f')}")
                        elif metadata.get("weight_kg") is not None:
                            w_state = "LOCK" if metadata.get("weight_stable") else "moving"
                            notice += f" | W: {format_metric(metadata.get('weight_kg'), 'kg', '.2f')} ({w_state})"
                        if phase in {"STANDBY", "DRAINING", "RESULT_HOLD"}:
                            last_image = 0
                    elif kind == "status":
                        message = metadata.get("message", "Waiting")
                        if message != notice:
                            print(f"[UNO] {message}", flush=True)
                        notice = message
                        # Keep the last video frame on screen during brief Wi-Fi
                        # hiccups; only switch to the black screen if > 6 s old.
                        if last_image and time.monotonic() - last_image > 6:
                            last_image = 0
                    elif kind == "frame" and jpeg:
                        latest_frame = (metadata, jpeg)

                if awaiting_config_ack_deadline and time.monotonic() > awaiting_config_ack_deadline:
                    print("[CANH BAO] Chua nhan duoc phan hoi xac nhan cau hinh tu YOLO UNO sau 5 giay.", flush=True)
                    awaiting_config_ack_deadline = None

                if latest_frame is not None:
                    metadata, jpeg = latest_frame
                    image = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
                    if image is not None:
                        box_color = framing_box_color(metadata.get("framing"))
                        for box in metadata.get("boxes", []):
                            if len(box) < 5:
                                continue
                            x1, y1, x2, y2, confidence = box[:5]
                            p1 = (round(x1), round(y1))
                            p2 = (round(x2), round(y2))
                            cv2.rectangle(image, p1, p2, box_color, 2)
                            cv2.putText(image, f"{confidence:.2f}", (p1[0], max(15, p1[1] - 6)),
                                        cv2.FONT_HERSHEY_SIMPLEX, 0.45, box_color, 1)
                        canvas = cv2.resize(image, (800, round(image.shape[0] * 800 / image.shape[1])))
                        timing = format_timing_line(metadata)
                        weight_kg = metadata.get("weight_kg")
                        weight_state = ("wait scale" if weight_kg is None
                                        else ("LOCK" if metadata.get("weight_stable") else "moving"))
                        hold_tag = " [HOLD]" if metadata.get("held") else ""
                        lines = [
                            f"{metadata.get('status', '')}{hold_tag}",
                            f"Height: {format_metric(metadata.get('height_cm'), 'cm', '.1f')}   Distance: {format_metric(metadata.get('distance_m'), 'm', '.2f')}",
                            f"Weight: {format_metric(weight_kg, 'kg', '.2f')} ({weight_state})   BMI: {format_metric(metadata.get('bmi'), '', '.2f')}",
                            timing,
                        ]
                        servo_line = format_servo_line(metadata)
                        if servo_line is not None:
                            lines.append(servo_line)
                        hud_h = min(canvas.shape[0], 15 + 25 * len(lines))
                        overlay = canvas[:hud_h].copy()
                        overlay[:] = (0, 0, 0)
                        cv2.addWeighted(overlay, 0.45, canvas[:hud_h], 0.55, 0, canvas[:hud_h])
                        for i, line in enumerate(lines):
                            cv2.putText(canvas, line, (10, 25 + 25 * i), cv2.FONT_HERSHEY_SIMPLEX, .55, (0, 255, 255), 2)
                        background = canvas
                        last_image = time.monotonic()

                if not last_image or time.monotonic() - last_image > 15:
                    screen = np.zeros((480, 800, 3), np.uint8)
                    text = notice if not last_image else "No fresh frame from YOLO UNO"
                    # Wrap long diagnostics (reason | URL | UNO IP) to fit the window.
                    lines = [part.strip() for part in text.split("|")]
                    wrapped = []
                    for line in lines:
                        while len(line) > 60:
                            cut = line.rfind(" ", 0, 60)
                            cut = cut if cut > 0 else 60
                            wrapped.append(line[:cut])
                            line = line[cut:].strip()
                        wrapped.append(line)
                    for i, line in enumerate(wrapped[:14]):
                        cv2.putText(screen, line, (12, 50 + 30 * i),
                                    cv2.FONT_HERSHEY_SIMPLEX, .65, (0, 255, 255), 2)
                else:
                    screen = background
                cv2.imshow(title, screen)
                key = cv2.waitKey(5) & 0xff
                if key in (ord('q'), ord('Q'), 27) or cv2.getWindowProperty(title, cv2.WND_PROP_VISIBLE) < 1:
                    break
    except KeyboardInterrupt:
        pass
    finally:
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
