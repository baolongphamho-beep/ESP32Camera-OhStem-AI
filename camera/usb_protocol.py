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
# Hard servo travel 45..135 deg (team decision 22/11), same as SWEEP_LIMIT_MIN/MAX_DEG.
SERVO_TRAVEL = (45.0, 135.0)
SERVO_LIMITS = {
    "servo_pin": ("int", 0, 48),         # GPIO number; reserved pins are NOT blocked (22/11)
    "servo_bottom": ("float", 45, 135),  # servo command (deg) where the sweep starts
    "servo_top": ("float", 45, 135),     # servo command (deg) where the sweep ends
    "servo_step": ("float", 0.5, 20),    # deg per step-scan move
    "servo_level": ("float", 45, 135),    # servo command with the camera level
    "servo_dir": ("int", -1, 1),         # TV3: tilt = dir*(cmd-level); -1 = larger angle looks UP
    "servo_settle": ("int", 0, 3000),    # ms to wait after the servo STOPS before a frame
    "servo_delay": ("int", 0, 3000),     # ms: age of the camera image (TV3 servoFrameDelayMs)
    "servo_dps": ("float", 1, 60),       # deg/s of the native continuous sweep (TV3)
    "hold_sec": ("float", 0, 120),       # s to keep H/W/BMI after stepping off
}
# Camera image settings the native UNO pushes to the OhStem camera via /control
# (firmware/edge_yolo/main/camera_control.h). -1 = leave the camera as it is.
# framesize on the LAB camera: 5 = 320x240, 8 = 640x480 (observed); quality: lower = sharper.
CAMERA_LIMITS = {
    "cam_framesize": ("int", -1, 10),
    "cam_quality": ("int", -1, 63),      # -1 or 4..63
}
CAMERA_FRAMESIZES = {5: "320x240 (QVGA)", 6: "400x296 (CIF)", 7: "480x320 (HVGA)", 8: "640x480 (VGA)"}
NUMERIC_LIMITS = {**SERVO_LIMITS, **CAMERA_LIMITS}
# USB (19/20), flash/PSRAM (26-37), UART0 (43/44), LCD I2C (11/12): no longer rejected
# (team decision 22/11, like TV3's driver); usb_viewer.py only prints a warning.
FORBIDDEN_SERVO_PINS = frozenset({11, 12, 19, 20, 43, 44, *range(26, 38)})
# Plan 2 calls the scale MAC "scaleMac"; on the wire it stays the existing "mac" key.
KEY_ALIASES = {"scaleMac": "mac"}
MAC_RE = re.compile(r"[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}")


def _number(key, value):
    kind, low, high = NUMERIC_LIMITS[key]
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
        if key == "cam_quality" and 0 <= number < 4:
            raise ValueError("cam_quality must be -1 or 4..63")
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
        if wire in NUMERIC_LIMITS:
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


CONFIG_QUERY = b"CONFIG?\n"
CONFIG_LABELS = [
    ("servo_pin", "Chan servo (GPIO)"),
    ("servo_level", "Goc servo khi camera nam ngang"),
    ("servo_dir", "Chieu quay"),
    ("servo_bottom", "Bat dau quet (camera nhin thap)"),
    ("servo_top", "Ket thuc quet (camera nhin cao)"),
    ("servo_step", "Buoc quet (do)"),
    ("servo_settle", "Cho sau khi servo dung (ms)"),
    ("servo_delay", "Do tre anh camera (ms)"),
    ("servo_dps", "Toc do quet lien tuc (do/s)"),
    ("hold_sec", "Giu ket qua (s)"),
    ("height", "Truc servo cao hon mat can (cm)"),
    ("focal", "fy/H"),
    ("mac", "MAC can"),
    ("camera", "URL camera"),
    ("cam_framesize", "Do phan giai camera (-1 = khong doi)"),
    ("cam_quality", "Chat luong JPEG (-1 = khong doi)"),
    ("ssid", "Wi-Fi SSID"),
    ("password_set", "Da co mat khau Wi-Fi"),
]


def format_config(meta):
    """Lines for a {"kind":"config_values"} reply to CONFIG? (no password is ever sent)."""
    if not isinstance(meta, dict) or meta.get("kind") != "config_values":
        return ["Khong phai goi cau hinh."]
    if meta.get("error"):
        return [f"UNO bao loi: {meta['error']}"]
    lines = []
    for key, label in CONFIG_LABELS:
        if key not in meta:
            continue
        value = meta[key]
        if key == "servo_dir" and value in (-1, 1):
            value = f"{value} ({'tang goc -> camera NGANG LEN' if value == -1 else 'tang goc -> camera CUI XUONG'})"
        elif key == "password_set":
            value = "co" if value is True else "chua"
        elif isinstance(value, float):
            value = f"{value:g}"
        lines.append(f"  {label:<38} {key} = {value}")
    limits = meta.get("servo_limits")
    if isinstance(limits, list) and len(limits) == 2:
        lines.append(f"  {'Gioi han cung cua servo':<38} {limits[0]:g}..{limits[1]:g} do")
    return lines or ["Goi cau hinh rong."]


def config_set_args(meta, keys=("servo_level", "servo_dir", "servo_bottom", "servo_top")):
    """'--set servo_level=92 --set ...' to re-apply the read values (calibration backup)."""
    parts = []
    for key in keys:
        if key in meta and meta[key] is not None:
            value = meta[key]
            parts.append(f"--set {key}={value:g}" if isinstance(value, float) else f"--set {key}={value}")
    return " ".join(parts)
