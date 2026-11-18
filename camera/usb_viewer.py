"""View JPEGs/results from YOLO UNO. No model or inference runs on this PC."""
import argparse
import time

from usb_protocol import Parser, config_commands


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
            if low <= value <= high or (zero and value == 0):
                return value
        except ValueError:
            pass
        print(f"Nhap so trong khoang {low}..{high}.")


def configure():
    values = {}
    ssid = input("WiFi SSID (Enter = giu nguyen): ").strip()
    if ssid:
        values["ssid"] = ssid
        values["password"] = input("WiFi password (khong ghi file): ")
    url = input("URL hoac IP camera (Enter = giu nguyen, vi du http://10.127.7.68/capture): ").strip()
    if url:
        if not url.startswith(("http://", "https://")):
            url = "http://" + url
        if not url.startswith("http://"):
            raise ValueError("Camera URL phai bat dau bang http://")
        host_and_path = url[len("http://"):].rstrip("/")
        if "/" not in host_and_path:
            url = f"http://{host_and_path}/capture"
            print(f"  -> Tu dong them /capture: {url}")
        values["camera"] = url
    mode = input("Che do fixed / ground [fixed]: ").strip().lower() or "fixed"
    if mode not in {"fixed", "ground"}:
        raise ValueError("Chi chap nhan fixed hoac ground")
    values["mode"] = mode
    print("fy/H = tieu cu pixel chia chieu cao anh; KHONG nhap tieu cu mm.")
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
    return config_commands(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Vi du COM9")
    parser.add_argument("--configure", action="store_true", help="Nhap WiFi va thong so lap dat")
    args = parser.parse_args()
    import cv2
    import numpy as np
    decoder = Parser()
    title = "SmartScale - YOLO UNO USB viewer (Q to quit)"
    background = np.zeros((480, 800, 3), np.uint8)
    last_image = 0
    notice = "Waiting for YOLO UNO..."
    try:
        with open_device(args.port) as port:
            print(f"Da mo {args.port}.", flush=True)
            commands = configure() if args.configure else None
            # Do not toggle DTR/RTS: this board uses native USB Serial/JTAG.
            if commands:
                port.reset_input_buffer()
                port.write(commands)
            while True:
                waiting = port.in_waiting
                frames = decoder.feed(port.read(min(waiting, 65536))) if waiting else []
                for metadata, jpeg in frames:
                    kind = metadata.get("kind")
                    if kind == "config":
                        print("Da luu cau hinh." if metadata.get("ok") else "Cau hinh bi tu choi.")
                        if metadata.get("ok") and not last_image:
                            notice = "Configuration saved. Fetching camera frame..."
                    elif kind == "boot":
                        print("On-board self-test:", metadata)
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
                        image = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
                        if image is None:
                            continue
                        for x1, y1, x2, y2, confidence in metadata.get("boxes", []):
                            cv2.rectangle(image, (round(x1), round(y1)), (round(x2), round(y2)), (0, 255, 0), 2)
                        canvas = cv2.resize(image, (800, round(image.shape[0] * 800 / image.shape[1])))
                        cap_ms = metadata.get("capture_ms")
                        timing = (f"AI: {metadata.get('processing_ms')} ms   Net: {cap_ms} ms"
                                  if cap_ms is not None else
                                  f"On-board processing: {metadata.get('processing_ms')} ms")
                        lines = [metadata.get("status", ""),
                                 f"Height: {metadata.get('height_cm')} cm   Distance: {metadata.get('distance_m')} m",
                                 f"Weight: {metadata.get('weight_kg')} kg {'LOCK' if metadata.get('weight_stable') else 'moving'}   BMI: {metadata.get('bmi')}",
                                 timing]
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
