"""Shared TV5 power diagnostics and local, image-free packet logging."""
import json
import math
from pathlib import Path
import time

POWER_KEYS = frozenset({"cpu_mhz", "ble_duty_percent", "wifi_ps", "wifi_started",
                        "wifi_ready", "pm_enabled", "light_sleep_enabled",
                        "usb_sleep_blocked", "no_ls_locks"})
LOG_KEYS = POWER_KEYS | frozenset({
    "kind", "phase", "session_id", "preview", "width", "height", "framing", "status",
    "height_cm", "distance_m", "weight_kg", "weight_stable", "bmi", "servo_deg", "tilt_deg",
    "processing_ms", "capture_ms", "decode_ms", "preprocess_ms", "inference_ms",
    "postprocess_ms", "free_psram", "free_internal", "self_test_ms", "ok", "active"})


def number(value):
    try:
        return (value if isinstance(value, (int, float)) and not isinstance(value, bool)
                and math.isfinite(value) else None)
    except OverflowError:
        return None


def display(value, suffix=""):
    value = number(value)
    return "--" if value is None else f"{value:g}{suffix}"


def sleep_status(meta):
    configured = meta.get("light_sleep_enabled")
    if configured is False:
        return "not configured"
    if configured is not True:
        return "unknown (no telemetry)"
    if meta.get("usb_sleep_blocked") is True:
        return "configured; blocked by USB"
    locks = number(meta.get("no_ls_locks"))
    if locks is not None and locks > 0:
        return "configured; blocked by PM locks"
    if meta.get("usb_sleep_blocked") is False and locks == 0:
        return "configured; actual sleep unmeasured"
    return "configured; blocking data unknown"


def power_lines(meta):
    wifi = meta.get("wifi_ps")
    wifi = wifi if isinstance(wifi, str) and wifi in {"NONE", "MIN_MODEM", "MAX_MODEM"} else "--"
    started, ready = meta.get("wifi_started"), meta.get("wifi_ready")
    network = "not started" if started is False else (
        "connected" if ready is True else "not connected" if ready is False else "state unknown")
    pm = "on" if meta.get("pm_enabled") is True else "off" if meta.get("pm_enabled") is False else "--"
    locks = number(meta.get("no_ls_locks"))
    return [f"WiFi PS: {wifi} | {network} | PM: {pm}",
            f"Light sleep: {sleep_status(meta)}",
            f"NO_LIGHT_SLEEP locks: {display(locks if locks is not None and locks >= 0 else None)}"]


class PacketRecorder:
    """Append JSONL metadata only. Never log config input, passwords or JPEGs.

    host_ms is elapsed monotonic receive time, not a timestamp from the UNO.
    A new recording run has an explicit boundary even when appending a file.
    """
    def __init__(self, path=None, clock=time.monotonic):
        self.path, self.clock, self.file = path, clock, None

    def __enter__(self):
        self.started = self.clock()
        if self.path is not None:
            path = Path(self.path)
            path.parent.mkdir(parents=True, exist_ok=True)
            self.file = path.open("a", encoding="utf-8")
            self.file.write(json.dumps({"kind": "recording_start", "schema": "tv5-ssv1-v1"}) + "\n")
            self.file.flush()
        return self

    def write(self, metadata, jpeg_length=0):
        if not self.file:
            return
        safe = {}
        for key in LOG_KEYS:
            value = metadata.get(key)
            if value is None or isinstance(value, bool) or number(value) is not None:
                if key in metadata:
                    safe[key] = value
            elif isinstance(value, str):
                safe[key] = value[:160]
        # Coordinates/images are unnecessary for timing QA.
        boxes = metadata.get("boxes")
        if isinstance(boxes, list):
            safe["people_count"] = len(boxes)
        row = {"host_ms": round((self.clock() - self.started) * 1000),
               "meta": safe, "jpeg_bytes": jpeg_length}
        self.file.write(json.dumps(row, allow_nan=False) + "\n")
        self.file.flush()

    def __exit__(self, *exc):
        if self.file:
            self.file.close()
