"""Match OhStem's browser /control serial command, including literal semicolons."""
import math
import os
from pathlib import Path
import re


def ultralytics_model_path(path):
    """Path to give ultralytics.YOLO() for a local .pt file.

    Ultralytics cleans absolute paths and drops some characters (the apostrophe in
    "C:\\Users\\Mira's Laptop"), then believes the file is missing and tries to
    DOWNLOAD it. A path relative to the current folder avoids that; a missing file
    is an error here instead of a silent download.
    """
    path = Path(path).resolve()
    if not path.is_file():
        raise FileNotFoundError(f"YOLO model not found: {path}")
    try:
        return os.path.relpath(path)
    except ValueError:  # Different drive on Windows: no relative path exists.
        return str(path)


def serial_control_url(base_url, label, confidence):
    # Restrict to our machine labels; never allow a label to inject URL/protocol fields.
    if not re.fullmatch(r"[A-Z0-9_.-]+", label):
        raise ValueError("Invalid camera label")
    if not math.isfinite(confidence) or not 0 <= confidence <= 1:
        raise ValueError("Confidence must be between 0 and 1")
    # Do not use requests params={...}: it encodes delimiters as %3B. The camera's
    # original UI uses /control?serial=LABEL;probability;stop and ESP-IDF's query
    # extraction does not automatically URL-decode the value.
    return f"{base_url.rstrip('/')}/control?serial={label};{confidence:.3f};stop"


PEDESTRIAN_MAX_M = 50.0


def pedestrian_label(distance_m, safe_distance_m):
    """Project 3 label decoded by lib/Measurements/pedestrian_state.h.

    PED_WARN_D1.80 when closer than safe_distance_m, else PED_SAFE_D3.20.
    The distance is clamped to (0.01, 50] m so the UNO always accepts it.
    """
    if not math.isfinite(distance_m) or distance_m <= 0:
        raise ValueError("distance must be a positive finite number")
    d = min(max(distance_m, 0.01), PEDESTRIAN_MAX_M)
    kind = "WARN" if distance_m < safe_distance_m else "SAFE"
    return f"PED_{kind}_D{d:.2f}"
