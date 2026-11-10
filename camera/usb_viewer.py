"""View JPEGs/results from YOLO UNO. No model or inference runs on this PC."""
import argparse
import getpass
import time

from usb_protocol import Parser, config_commands


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
        values["password"] = getpass.getpass("WiFi password (khong hien / khong ghi file): ")
    url = input("URL anh camera (Enter = giu nguyen, vi du http://10.127.7.68/capture): ").strip()
    if url:
        if not url.startswith("http://"):
            raise ValueError("Camera URL phai bat dau bang http://")
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
    with open_device(args.port) as port:
        print(f"Da mo {args.port}.", flush=True)
        commands = configure() if args.configure else None
        # Do not toggle DTR/RTS: this board uses native USB Serial/JTAG.
        if commands:
            port.write(commands)
        while True:
            frames = decoder.feed(port.read(min(max(port.in_waiting, 1), 65536)))
            for metadata, jpeg in frames:
                kind = metadata.get("kind")
                if kind == "config":
                    print("Da luu cau hinh." if metadata.get("ok") else "Cau hinh bi tu choi.")
                elif kind == "boot":
                    print("On-board self-test:", metadata)
                elif kind == "status":
                    notice = metadata.get("message", "Waiting")
                    last_image = 0
                elif kind == "frame" and jpeg:
                    image = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
                    if image is None:
                        continue
                    for x1, y1, x2, y2, confidence in metadata.get("boxes", []):
                        cv2.rectangle(image, (round(x1), round(y1)), (round(x2), round(y2)), (0, 255, 0), 2)
                    canvas = cv2.resize(image, (800, round(image.shape[0] * 800 / image.shape[1])))
                    lines = [metadata.get("status", ""),
                             f"Height: {metadata.get('height_cm')} cm   Distance: {metadata.get('distance_m')} m",
                             f"Weight: {metadata.get('weight_kg')} kg {'LOCK' if metadata.get('weight_stable') else 'moving'}   BMI: {metadata.get('bmi')}",
                             f"On-board processing: {metadata.get('processing_ms')} ms"]
                    for i, line in enumerate(lines):
                        cv2.putText(canvas, line, (10, 25 + 25 * i), cv2.FONT_HERSHEY_SIMPLEX, .55, (0, 255, 255), 2)
                    background = canvas
                    last_image = time.monotonic()
            if not last_image or time.monotonic() - last_image > 15:
                screen = np.zeros((480, 800, 3), np.uint8)
                cv2.putText(screen, notice if not last_image else "No fresh frame from YOLO UNO", (12, 50),
                            cv2.FONT_HERSHEY_SIMPLEX, .65, (0, 255, 255), 2)
            else:
                screen = background
            cv2.imshow(title, screen)
            if cv2.waitKey(1) & 0xff in (ord('q'), 27):
                break
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
