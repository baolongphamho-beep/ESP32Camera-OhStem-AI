"""Local camera + scale workbench. No firmware writes. Python 3.10+."""
import argparse
import json
import math
import re
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STATE = {"weight_kg": None, "received_at": 0, "status": "Chưa kết nối cân", "source": "none"}
LOCK = threading.Lock()

def parse_weight(line, legacy_unit=None):
    """JSON contract is preferred; legacy logs need explicit unit selection."""
    try:
        obj = json.loads(line)
        if obj.get("type") != "weight":
            return None
        value = float(obj["weight_kg"])
    except (ValueError, TypeError, KeyError, AttributeError):
        match = re.search(r"\[MiScale2\] Weight received: ([0-9.]+)", line)
        if not match or legacy_unit is None:
            return None
        value = float(match[1]) * (0.45359237 if legacy_unit == "lb" else 1)
    if not math.isfinite(value) or not 0 < value <= 300:
        return None
    return value

def height_m(h, distance, alpha, fy, cy, top):
    values = (h, distance, alpha, fy, cy, top)
    if not all(math.isfinite(x) for x in values) or h <= 0 or distance <= 0 or fy <= 0:
        raise ValueError("Thông số hình học không hợp lệ")
    angle = math.radians(alpha) + math.atan((cy - top) / fy)
    if abs(angle) >= math.radians(85):
        raise ValueError("Góc nhìn ngoài phạm vi phép đo")
    result = h + distance * math.tan(angle)
    if not 0.3 <= result <= 2.5:
        raise ValueError("Chiều cao ngoài 0.3–2.5 m; kiểm tra hiệu chuẩn")
    return result

def serial_loop(port, baud, legacy_unit):
    try:
        import serial
    except ImportError:
        with LOCK:
            STATE["status"] = "Thiếu pyserial: python -m pip install pyserial"
        return
    while True:
        try:
            with serial.Serial(port, baud, timeout=1) as stream:
                with LOCK:
                    STATE.update(status=f"Đã mở {port}; chờ cân", weight_kg=None, received_at=0)
                while True:
                    line = stream.readline(2048).decode("utf-8", errors="replace").strip()
                    try:
                        value = parse_weight(line, legacy_unit)
                    except ValueError:
                        continue
                    if value is not None:
                        with LOCK:
                            STATE.update(weight_kg=value, received_at=time.time(), source="serial", status="Đang nhận cân; chưa xác nhận độ ổn định")
        except (serial.SerialException, OSError) as error:
            with LOCK:
                STATE.update(status=f"Mất kết nối: {error}", weight_kg=None, received_at=0)
            time.sleep(2)

class Handler(BaseHTTPRequestHandler):
    def send(self, status, data, mime="application/json; charset=utf-8"):
        if not isinstance(data, bytes):
            data = json.dumps(data, ensure_ascii=False, allow_nan=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            return self.send(200, (ROOT / "index.html").read_bytes(), "text/html; charset=utf-8")
        if path == "/api/state":
            with LOCK:
                result = dict(STATE)
            result["fresh"] = time.time() - result["received_at"] < 5
            return self.send(200, result)
        if path == "/api/capture":
            if not self.server.camera_url:
                return self.send(400, {"error": "Chưa đặt --camera-url; có thể tải ảnh từ máy để thử"})
            try:
                request = urllib.request.Request(self.server.camera_url, headers={"Cache-Control": "no-cache"})
                with urllib.request.urlopen(request, timeout=6) as response:
                    mime = response.headers.get_content_type()
                    data = response.read(8 * 1024 * 1024 + 1)
                if mime not in ("image/jpeg", "image/png") or len(data) > 8 * 1024 * 1024:
                    raise ValueError("Endpoint phải trả ảnh JPEG/PNG, tối đa 8 MB")
                return self.send(200, data, mime)
            except Exception as error:
                return self.send(502, {"error": str(error)})
        self.send(404, {"error": "Not found"})

    def do_POST(self):
        if self.path != "/api/height":
            return self.send(404, {"error": "Not found"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 4096:
                raise ValueError("Yêu cầu quá lớn hoặc rỗng")
            obj = json.loads(self.rfile.read(length))
            result = height_m(*(float(obj[k]) for k in ("h", "distance", "alpha", "fy", "cy", "top")))
            self.send(200, {"height_m": result})
        except (ValueError, KeyError, TypeError) as error:
            self.send(400, {"error": str(error)})

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera-url", default="", help="Ví dụ http://192.168.1.20/capture")
    parser.add_argument("--serial-port", default="")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--legacy-unit", choices=("kg", "lb"), help="Chỉ dùng khi đọc log repo gốc và đã xác minh đơn vị")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if args.camera_url and not args.camera_url.startswith(("http://", "https://")):
        parser.error("camera-url phải là HTTP/HTTPS")
    if args.serial_port:
        threading.Thread(target=serial_loop, args=(args.serial_port, args.baud, args.legacy_unit), daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.camera_url = args.camera_url
    print(f"Mini Project: http://127.0.0.1:{args.port} - Ctrl+C to stop", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
