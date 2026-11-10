"""JPEG + on-board results, framed independently of USB read boundaries."""
import json
import struct
import zlib

HEADER = struct.Struct("<4sIII")
MAGIC = b"SSV1"
MAX_JSON = 4096
MAX_JPEG = 256 * 1024


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
    limits = {"ssid": 32, "password": 64, "camera": 159, "mac": 17}
    for key, value in values.items():
        if key not in {"ssid", "password", "camera", "mac", "mode", "distance", "reference", "focal", "height", "tilt"}:
            raise ValueError("Unknown configuration key")
        text = str(value)
        if "\r" in text or "\n" in text or "\x00" in text:
            raise ValueError("Configuration cannot contain line breaks or NUL")
        if len(text.encode()) > limits.get(key, 64):
            raise ValueError(f"{key} is too long")
        commands.append(f"CFG {key}={text}")
    return ("\n".join(commands + ["APPLY", ""]).encode())
