"""Calibrate the Plan 2 servo by hand over USB (native firmware, USB "SERVO <deg>").

  py -3 tools/servo_jog.py                 # tu tim cong YOLO UNO
  py -3 tools/servo_jog.py --port COM7 --start 90

Lenh (go roi Enter):
  90        quay toi 90 do (chi 45..135)         + / -     them / bot 1 do      +5 / -5   them / bot 5 do
  level     ghi goc hien tai = servo_level (camera nam ngang)
  bottom    ghi goc hien tai = servo_bottom (nhin thap nhat, van thay chan tren mat can)
  top       ghi goc hien tai = servo_top    (nhin cao nhat can dung)
  up        TANG goc lam camera NGANG LEN  -> servo_dir = -1
  down      TANG goc lam camera CUI XUONG  -> servo_dir = 1
  show      xem cac gia tri da ghi          save      gui cac gia tri da ghi xuong UNO (luu NVS)
  config    doc lai cau hinh DANG LUU tren UNO (kiem tra sau khi save)
  q         nha servo va thoat

The firmware holds the angle only while this tool renews the lease (every 3 s); if the
tool stops or USB is lost, the servo is released and Plan 2 resumes after 10 s.
Power the servo from its own 5 V supply with GND shared with the UNO.
"""
import argparse
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "camera"))
from serial_port import add_port_argument, port_or_exit  # noqa: E402
from usb_protocol import CONFIG_QUERY, SERVO_TRAVEL, Parser, config_commands, format_config  # noqa: E402


class JogSession:
    """Pure command logic (tested without a board)."""

    def __init__(self, start=90.0, step=1.0):
        self.deg = self._clamp(start)
        self.step = step
        self.marks = {}

    @staticmethod
    def _clamp(deg):
        low, high = SERVO_TRAVEL
        return max(low, min(high, round(float(deg), 1)))

    def servo_line(self):
        text = f"{self.deg:.1f}".rstrip("0").rstrip(".")
        return f"SERVO {text}\n".encode()

    def handle(self, text):
        """-> (bytes to send or None, message, keep_running)."""
        cmd = text.strip().lower()
        if not cmd:
            return None, "", True
        if cmd in ("q", "quit", "exit"):
            return b"SERVO OFF\n", "Nha servo.", False
        if cmd in ("+", "-"):
            cmd += str(self.step)
        if cmd[0] in "+-" and len(cmd) > 1:
            try:
                delta = float(cmd)
            except ValueError:
                return None, f"Khong hieu: {text!r}", True
            self.deg = self._clamp(self.deg + delta)
            return self.servo_line(), f"Goc {self.deg:g}", True
        try:
            value = float(cmd)
        except ValueError:
            value = None
        if value is not None:
            low, high = SERVO_TRAVEL
            if not low <= value <= high:
                return None, f"Goc phai trong {low:g}..{high:g}", True
            self.deg = self._clamp(value)
            return self.servo_line(), f"Goc {self.deg:g}", True
        if cmd in ("level", "bottom", "top"):
            self.marks[f"servo_{cmd}"] = self.deg
            return None, f"servo_{cmd} = {self.deg:g}", True
        if cmd in ("up", "down"):
            self.marks["servo_dir"] = -1 if cmd == "up" else 1
            return None, f"servo_dir = {self.marks['servo_dir']}", True
        if cmd == "show":
            return None, self.summary(), True
        if cmd == "config":
            return CONFIG_QUERY, "Dang doc cau hinh tren UNO...", True
        if cmd == "save":
            if not self.marks:
                return None, "Chua ghi gia tri nao (level/bottom/top/up/down).", True
            try:
                payload = config_commands(dict(self.marks))
            except ValueError as error:
                return None, f"Khong luu: {error}", True
            return payload, "Gui cau hinh: " + self.summary(), True
        return None, f"Khong hieu: {text!r} (go 'show', 'save' hoac 'q')", True

    def summary(self):
        if not self.marks:
            return "(chua ghi gi)"
        return " ".join(f"{k}={v:g}" for k, v in self.marks.items())


def main():
    import serial

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    add_port_argument(ap)
    ap.add_argument("--start", type=float, default=90.0, help="Goc dau tien (chon goc an toan, giua hanh trinh)")
    ap.add_argument("--step", type=float, default=1.0)
    args = ap.parse_args()
    if not SERVO_TRAVEL[0] <= args.start <= SERVO_TRAVEL[1] or not 0 < args.step <= 30:
        ap.error("--start 45..135, --step 0..30")
    port = port_or_exit(args.port)
    session = JogSession(args.start, args.step)
    lock = threading.Lock()
    stop = threading.Event()
    with serial.Serial(port, 115200, timeout=0.2, write_timeout=3) as dev:
        def send(data):
            with lock:
                dev.write(data)
                dev.flush()

        def heartbeat():
            while not stop.wait(3.0):
                send(session.servo_line())

        def reader():
            parser = Parser()
            while not stop.is_set():
                try:
                    chunk = dev.read(4096)
                except Exception:  # noqa: BLE001 - port closed on exit
                    return
                for meta, _ in parser.feed(chunk):
                    kind = meta.get("kind")
                    if kind == "servo_jog" and meta.get("ok") is False:
                        print(f"\n[UNO] tu choi: {meta.get('error')}", flush=True)
                    elif kind == "config_values":
                        print("\n[UNO] Cau hinh dang luu:\n" + "\n".join(format_config(meta)), flush=True)
                    elif kind == "config":
                        print("\n[UNO] " + ("Da luu cau hinh." if meta.get("ok") else
                                            f"Cau hinh bi tu choi: {meta.get('error')}"), flush=True)
                    elif kind == "status" and meta.get("phase") == "SERVO_JOG" and meta.get("servo_deg") is None:
                        print("\n[UNO] servo chua gan (kiem tra servo_pin)", flush=True)

        print(f"Da mo {port}. Servo se quay toi {session.deg:g} do. Go lenh (xem --help), 'q' de thoat.")
        send(session.servo_line())
        threads = [threading.Thread(target=heartbeat, daemon=True), threading.Thread(target=reader, daemon=True)]
        for t in threads:
            t.start()
        try:
            running = True
            while running:
                data, message, running = session.handle(input(f"[{session.deg:g} do] > "))
                if data:
                    send(data)
                    if data.startswith(b"BEGIN"):
                        send(session.servo_line())  # APPLY resets the runtime; keep holding.
                if message:
                    print(message)
        except (KeyboardInterrupt, EOFError):
            send(b"SERVO OFF\n")
        finally:
            stop.set()
    print("Da thoat. Ket qua da ghi:", session.summary())
    return 0


if __name__ == "__main__":
    sys.exit(main())
