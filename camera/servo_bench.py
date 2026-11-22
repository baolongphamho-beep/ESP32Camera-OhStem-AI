"""Interactive USB Serial/JTAG bench tool for firmware -ServoTest mode."""
import argparse
import json
import math
import threading
import time
import zlib

from serial_port import add_port_argument, port_or_exit
from usb_protocol import HEADER, MAGIC, MAX_JPEG, MAX_JSON, config_commands


class StreamDemux:
    """Separate plain-text [SERVO] log lines from binary SSV1 packets."""

    def __init__(self):
        self.buffer = bytearray()
        self._text_buf = bytearray()

    def _drain_text_lines(self):
        lines = []
        while True:
            idx_n = self._text_buf.find(b"\n")
            idx_r = self._text_buf.find(b"\r")
            if idx_n < 0 and idx_r < 0:
                break
            if idx_r >= 0 and (idx_n < 0 or idx_r < idx_n):
                if idx_r == len(self._text_buf) - 1:
                    break
                raw_line = bytes(self._text_buf[:idx_r])
                step = 2 if self._text_buf[idx_r + 1:idx_r + 2] == b"\n" else 1
                del self._text_buf[:idx_r + step]
            else:
                raw_line = bytes(self._text_buf[:idx_n])
                del self._text_buf[:idx_n + 1]
            text = raw_line.rstrip(b"\r").decode("utf-8", "replace").strip("\x00")
            if text:
                lines.append(text)
        return lines

    def feed(self, data):
        self.buffer.extend(data)
        lines = []
        packets = []
        while self.buffer:
            start = self.buffer.find(MAGIC)
            if start < 0:
                tail_keep = 0
                for k in (3, 2, 1):
                    if len(self.buffer) >= k and self.buffer.endswith(MAGIC[:k]):
                        tail_keep = k
                        break
                take = len(self.buffer) - tail_keep
                if take > 0:
                    self._text_buf.extend(self.buffer[:take])
                    del self.buffer[:take]
                    lines.extend(self._drain_text_lines())
                break

            if start > 0:
                self._text_buf.extend(self.buffer[:start])
                del self.buffer[:start]
                lines.extend(self._drain_text_lines())

            if len(self.buffer) < HEADER.size:
                break

            _, size, image_size, crc = HEADER.unpack_from(self.buffer)
            if not 0 < size <= MAX_JSON or image_size > MAX_JPEG:
                self._text_buf.append(self.buffer[0])
                del self.buffer[0]
                lines.extend(self._drain_text_lines())
                continue

            end = HEADER.size + size + image_size
            if len(self.buffer) < end:
                break

            body = self.buffer[HEADER.size:end]
            if zlib.crc32(body) != crc:
                self._text_buf.append(self.buffer[0])
                del self.buffer[0]
                lines.extend(self._drain_text_lines())
                continue

            try:
                metadata = json.loads(body[:size])
                if isinstance(metadata, dict):
                    packets.append(metadata)
            except (ValueError, UnicodeError):
                pass
            del self.buffer[:end]

        return lines, packets


def parse_user_command(text):
    stripped = text.strip()
    if not stripped:
        return ("empty", None)

    lower = stripped.lower()
    if lower in {"q", "quit", "exit"}:
        return ("quit", None)

    if lower == "set" or lower.startswith("set "):
        tokens = stripped[3:].strip().split()
        if not tokens:
            raise ValueError(
                "Usage: set key=value [key=value ...], e.g. set servo_level=90 servo_dir=1"
            )
        values = {}
        for token in tokens:
            if "=" not in token:
                raise ValueError(f"Expected key=value token, got '{token}'")
            key, val = token.split("=", 1)
            if not key:
                raise ValueError(f"Missing key in token '{token}'")
            values[key] = val
        return ("config", config_commands(values))

    try:
        angle = float(stripped)
    except ValueError:
        angle = None
    if angle is not None and math.isfinite(angle):
        return ("raw", f"SERVO {stripped}\n".encode("utf-8"))

    shortcuts = {
        "help": "HELP",
        "?": "HELP",
        "status": "SERVO STATUS",
        "bottom": "SERVO BOTTOM",
        "top": "SERVO TOP",
        "level": "SERVO LEVEL",
        "sweep": "SERVO SWEEP",
        "detach": "SERVO DETACH",
    }
    cmd_text = shortcuts.get(lower, stripped)
    return ("raw", f"{cmd_text}\n".encode("utf-8"))


def format_packet_notice(meta):
    kind = meta.get("kind")
    if kind == "config":
        return "[CONFIG] Saved to NVS (OK)" if meta.get("ok") else "[CONFIG] Rejected by firmware"
    if kind == "status":
        return f"[UNO] {meta.get('message', '')}"
    if kind == "boot":
        return f"[BOOT] {json.dumps(meta, separators=(',', ':'))}"
    return None


def open_serial_no_reset(name):
    import serial
    port = serial.Serial(None, 115200, timeout=0.1, write_timeout=2.0, dsrdtr=False, rtscts=False)
    port.port = name
    port.dtr = False
    port.rts = False
    port.open()
    return port


def _pump_serial(port, demux, stop_event, print_lock):
    import serial
    while not stop_event.is_set():
        try:
            chunk = port.read(4096)
        except serial.SerialException as exc:
            with print_lock:
                print(f"[SERIAL ERROR] {exc}", flush=True)
            stop_event.set()
            break
        if not chunk:
            continue
        lines, packets = demux.feed(chunk)
        with print_lock:
            for line in lines:
                print(line, flush=True)
            for meta in packets:
                notice = format_packet_notice(meta)
                if notice:
                    print(notice, flush=True)


def main():
    import serial
    args_parser = argparse.ArgumentParser(description=__doc__)
    add_port_argument(args_parser)  # TV5: default auto-detect (or e.g. --port COM16)
    args_parser.add_argument("--cmd", help="Run a single command and exit (e.g. 'SERVO STATUS')")
    args_parser.add_argument("--timeout", type=float, default=1.5,
                             help="Seconds to collect output in --cmd mode (default: 1.5)")
    args = args_parser.parse_args()
    if args.timeout <= 0:
        args_parser.error("--timeout must be positive")
    args.port = port_or_exit(args.port)

    demux = StreamDemux()
    stop_event = threading.Event()
    print_lock = threading.Lock()
    try:
        with open_serial_no_reset(args.port) as port:
            reader = threading.Thread(
                target=_pump_serial, args=(port, demux, stop_event, print_lock), daemon=True
            )
            reader.start()
            if args.cmd is not None:
                kind, payload = parse_user_command(args.cmd)
                if kind in {"raw", "config"}:
                    port.write(payload)
                    port.flush()
                deadline = time.monotonic() + args.timeout
                while not stop_event.is_set() and time.monotonic() < deadline:
                    time.sleep(0.05)
                stop_event.set()
                reader.join(timeout=0.5)
                return 0

            print(
                f"Connected to {args.port} (-ServoTest bench). "
                "Type degree (e.g. 90), bottom/top/level/sweep/detach/status, "
                "'set servo_level=90 servo_dir=1', or quit.",
                flush=True,
            )
            port.write(b"SERVO STATUS\n")
            port.flush()
            while not stop_event.is_set():
                try:
                    raw = input()
                except EOFError:
                    break
                try:
                    kind, payload = parse_user_command(raw)
                except ValueError as exc:
                    with print_lock:
                        print(f"[INPUT ERROR] {exc}", flush=True)
                    continue
                if kind == "empty":
                    continue
                if kind == "quit":
                    break
                port.write(payload)
                port.flush()
            stop_event.set()
            reader.join(timeout=0.5)
    except KeyboardInterrupt:
        stop_event.set()
    except (serial.SerialException, ValueError) as exc:
        print(f"Error: {exc}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
