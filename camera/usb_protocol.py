"""JPEG + on-board results, framed independently of USB read boundaries."""
import json
import math
import re
import struct
import zlib

HEADER = struct.Struct("<4sIII")
MAGIC = b"SSV1"
MAX_JSON = 4096
MAX_JPEG = 256 * 1024


class SessionFilter:
    """Drop old pipeline generations; accept legacy firmware without IDs.

    Adapted from camera-power-integration@6c5a005. Plan 2 also sends phase
    changes as status packets; preview frames bypass the scale-phase gate.
    """
    def __init__(self):
        self.session = None
        self.phase = None

    def accept(self, metadata):
        if metadata.get("kind") == "boot":
            self.session = self.phase = None
            return True
        session = metadata.get("session_id")
        if not isinstance(session, int) or isinstance(session, bool):
            return True
        session &= 0xffffffff
        if self.session is not None and ((session - self.session) & 0xffffffff) >= 0x80000000:
            return False
        if session != self.session:
            self.session, self.phase = session, None
        if metadata.get("kind") in ("scale", "status") and isinstance(metadata.get("phase"), str):
            self.phase = metadata["phase"]
        return not (metadata.get("kind") == "frame" and metadata.get("preview") is not True
                    and self.phase in {"STANDBY", "DRAINING", "RESULT_HOLD"})


def packet(metadata, jpeg=b""):
    body = json.dumps(metadata, separators=(",", ":"), allow_nan=False).encode()
    return HEADER.pack(MAGIC, len(body), len(jpeg), zlib.crc32(body + jpeg)) + body + jpeg


class Parser:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while True:
            start = self.buffer.find(MAGIC)
            if start < 0:
                self.buffer[:] = self.buffer[-3:]
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < HEADER.size:
                break
            _, size, image_size, crc = HEADER.unpack_from(self.buffer)
            if not 0 < size <= MAX_JSON or image_size > MAX_JPEG:
                del self.buffer[0]
                continue
            end = HEADER.size + size + image_size
            if len(self.buffer) < end:
                # A timed-out USB transfer may be followed by a complete packet.
                # Accept a later marker only when its header AND CRC validate.
                offset = self.buffer.find(MAGIC, HEADER.size)
                recovered = False
                while offset >= 0 and len(self.buffer) >= offset + HEADER.size:
                    _, js, jpg, check = HEADER.unpack_from(self.buffer, offset)
                    tail = offset + HEADER.size + js + jpg
                    if (0 < js <= MAX_JSON and jpg <= MAX_JPEG and tail <= len(self.buffer)
                            and zlib.crc32(self.buffer[offset + HEADER.size:tail]) == check):
                        del self.buffer[:offset]
                        recovered = True
                        break
                    offset = self.buffer.find(MAGIC, offset + 1)
                if recovered:
                    continue
                break
            body = self.buffer[HEADER.size:end]
            if zlib.crc32(body) != crc:
                del self.buffer[0]
                continue
            try:
                metadata = json.loads(body[:size])
                if not isinstance(metadata, dict):
                    raise ValueError("metadata must be an object")
                frames.append((metadata, bytes(body[size:])))
            except (ValueError, UnicodeError):
                pass
            del self.buffer[:end]
        return frames


LEGACY_KEYS = {"ssid", "password", "camera", "mac", "mode", "distance", "reference",
               "focal", "height", "tilt"}
LEGACY_LIMITS = {"ssid": 32, "password": 64, "camera": 159, "mac": 17}

# Plan 2 interface contract (servo + result hold), stored by the firmware in NVS.
# key: (kind, low, high). Ranges are the TV5 proposal; TV3 (servo) / TV1 confirm.
SERVO_LIMITS = {
    "servo_pin": ("int", 0, 48),         # GPIO number, see FORBIDDEN_SERVO_PINS
    "servo_bottom": ("float", 0, 180),   # servo command (deg) where the sweep starts
    "servo_top": ("float", 0, 180),      # servo command (deg) where the sweep ends
    "servo_step": ("float", 0.5, 20),    # deg per step-scan move
    "servo_level": ("float", 0, 180),    # servo command with the camera level
    "servo_dir": ("int", -1, 1),         # +1 / -1 only: sign of tilt vs command
    "servo_settle": ("int", 0, 3000),    # ms to wait after a move before a frame
    "hold_sec": ("float", 0, 120),       # s to keep H/W/BMI after stepping off
}
# USB (19/20), flash/PSRAM (26-37), UART0 (43/44), LCD I2C (11/12).
FORBIDDEN_SERVO_PINS = frozenset({11, 12, 19, 20, 43, 44, *range(26, 38)})
# Plan 2 calls the scale MAC "scaleMac"; on the wire it stays the existing "mac" key.
KEY_ALIASES = {"scaleMac": "mac"}
MAC_RE = re.compile(r"[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}")


def _number(key, value):
    kind, low, high = SERVO_LIMITS[key]
    if isinstance(value, bool):
        raise ValueError(f"{key} must be a number")
    try:
        number = float(value)
    except (TypeError, ValueError):
        raise ValueError(f"{key} must be a number") from None
    if not math.isfinite(number) or not low <= number <= high:
        raise ValueError(f"{key} must be in {low}..{high}")
    if kind == "int":
        if number != int(number):
            raise ValueError(f"{key} must be a whole number")
        number = int(number)
        if key == "servo_dir" and number not in (-1, 1):
            raise ValueError("servo_dir must be 1 or -1")
        if key == "servo_pin" and number in FORBIDDEN_SERVO_PINS:
            raise ValueError(f"GPIO{number} is reserved (USB, flash/PSRAM, UART0 or LCD)")
        return str(number)
    text = f"{number:.3f}".rstrip("0").rstrip(".")  # 90.0 -> "90", 4.5 -> "4.5"
    return "0" if text == "-0" else text


def config_commands(values):
    """BEGIN / CFG key=value ... / APPLY, as the YOLO UNO firmware reads them.

    Values that are None are skipped (nothing is sent for them). Old keys are
    serialized exactly as before; new servo / hold keys are type and range checked.
    """
    commands = ["BEGIN"]
    seen = {}
    for key, value in values.items():
        if value is None:
            continue
        wire = KEY_ALIASES.get(key, key)
        if wire in SERVO_LIMITS:
            text = _number(wire, value)
        elif wire in LEGACY_KEYS:
            text = str(value)
            if "\r" in text or "\n" in text or "\x00" in text:
                raise ValueError("Configuration cannot contain line breaks or NUL")
            if len(text.encode()) > LEGACY_LIMITS.get(wire, 64):
                raise ValueError(f"{wire} is too long")
            if wire == "mac" and not MAC_RE.fullmatch(text):
                raise ValueError("mac must look like AA:BB:CC:DD:EE:FF")
        else:
            raise ValueError("Unknown configuration key")
        if wire in seen:
            if seen[wire] != text:
                raise ValueError(f"{key} given twice with different values")
            continue
        seen[wire] = text
        commands.append(f"CFG {wire}={text}")
    if "servo_bottom" in seen and "servo_top" in seen and \
            float(seen["servo_bottom"]) == float(seen["servo_top"]):
        raise ValueError("servo_bottom and servo_top must differ")
    return ("\n".join(commands + ["APPLY", ""]).encode())
