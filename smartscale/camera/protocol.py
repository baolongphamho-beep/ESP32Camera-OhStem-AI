"""Match OhStem's browser /control serial command, including literal semicolons."""
import math
import re


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
