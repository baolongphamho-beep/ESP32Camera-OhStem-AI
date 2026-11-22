"""JPEG + on-board results, framed independently of USB read boundaries."""
import json
import struct
import zlib

HEADER = struct.Struct("<4sIII")
MAGIC = b"SSV1"
MAX_JSON = 4096
MAX_JPEG = 256 * 1024


class SessionFilter:
    """Ignore old pipeline frames after a new scale-session/status packet."""
    def __init__(self):
        self.session = None
        self.phase = None

    def accept(self, metadata):
        if metadata.get("kind") == "boot":
            self.session = None
            self.phase = None
            return True
        session = metadata.get("session_id")
        if not isinstance(session, int) or isinstance(session, bool):
            return True  # Compatible with older firmware.
        session &= 0xffffffff
        if self.session is not None:
            delta = (session - self.session) & 0xffffffff
            if delta >= 0x80000000:
                return False
        if session != self.session:
            self.session = session
            self.phase = None
        if metadata.get("kind") == "scale":
            self.phase = metadata.get("phase")
        if metadata.get("kind") == "frame" and self.phase in {
                "STANDBY", "DRAINING", "RESULT_HOLD"}:
            return False
        return True


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


def config_commands(values):
    commands = ["BEGIN"]
    limits = {
        "ssid": 32,
        "password": 64,
        "camera": 159,
        "mac": 17,
        "hold_sec": 3,
        "servo_pin": 4,
        "servo_dir": 2,
        "servo_settle": 4,
    }
    allowed_keys = {
        "ssid", "password", "camera", "mac", "mode", "distance",
        "reference", "focal", "height", "tilt", "hold_sec",
        "servo_pin", "servo_min", "servo_max", "servo_bottom",
        "servo_top", "servo_step", "servo_level", "servo_dir", "servo_settle",
    }
    for key, value in values.items():
        if key not in allowed_keys:
            raise ValueError("Unknown configuration key")
        text = str(value)
        if "\r" in text or "\n" in text or "\x00" in text:
            raise ValueError("Configuration cannot contain line breaks or NUL")
        if len(text.encode()) > limits.get(key, 64):
            raise ValueError(f"{key} is too long")
        commands.append(f"CFG {key}={text}")
    return ("\n".join(commands + ["APPLY", ""]).encode())


# ---- TV5: read back the configuration stored on the UNO (USB "CONFIG?") ----------
CONFIG_QUERY = b"CONFIG?\n"
CONFIG_LABELS = [
    ("ssid", "Wi-Fi SSID"),
    ("password_set", "Da co mat khau Wi-Fi"),
    ("camera", "URL camera"),
    ("mac", "MAC can"),
    ("mode", "Che do do (fixed/ground)"),
    ("distance", "Khoang cach camera - nguoi (m)"),
    ("reference", "Chieu cao nguoi chuan (cm)"),
    ("focal", "fy/H"),
    ("height", "Camera cao hon mat can (cm)"),
    ("tilt", "Goc nghieng co dinh (do)"),
    ("hold_sec", "Giu ket qua (s)"),
    ("servo_pin", "Chan servo (GPIO)"),
    ("servo_min", "Gioi han duoi servo (do)"),
    ("servo_max", "Gioi han tren servo (do)"),
    ("servo_bottom", "Bat dau quet (camera nhin thap)"),
    ("servo_top", "Ket thuc quet (camera nhin cao)"),
    ("servo_step", "Buoc quet (do)"),
    ("servo_level", "Goc servo khi camera nam ngang"),
    ("servo_dir", "Chieu quay"),
    ("servo_settle", "Cho sau moi lan quay (ms)"),
    ("revision", "So lan APPLY tu khi bat"),
]


def format_config(meta):
    """Lines for a {"kind":"config_values"} reply (the firmware never sends the password)."""
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
            value = f"{value} ({'tang goc -> camera CUI XUONG' if value == 1 else 'tang goc -> camera NGANG LEN'})"
        elif key == "password_set":
            value = "co" if value is True else "chua"
        elif isinstance(value, float):
            value = f"{value:g}"
        lines.append(f"  {label:<36} {key} = {value}")
    return lines or ["Goi cau hinh rong."]


def config_set_args(meta, keys=("servo_level", "servo_dir", "servo_bottom", "servo_top",
                                "servo_min", "servo_max", "height", "focal")):
    """'--set key=value ...' text to re-apply a calibration (no Wi-Fi password)."""
    parts = []
    for key in keys:
        if key in meta and meta[key] is not None:
            value = meta[key]
            parts.append(f"{key}={value:g}" if isinstance(value, float) else f"{key}={value}")
    return " ".join(parts)
