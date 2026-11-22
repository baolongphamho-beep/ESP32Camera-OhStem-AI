"""Pure logic for body_tracker.py: choose the person, build the UART label.

No OpenCV / YOLO here, so tests/test_body_protocol.py runs on any PC.
Must match lib/EdgeSweep/edge_message.h on the YOLO UNO:
    BOX_<x1>_<y1>_<x2>_<y2>_<conf>   box normalized to 0..1 (top-left origin), conf 0..100
    NOBODY                           no person
    MULTI                            two or more people of similar size
The UNO decides the framing (FullBody / TopClipped / ...) and the height from the servo
tilt, so the laptop only reports where the person is in the image.
"""
from dataclasses import dataclass
import math
import re

LABEL_RE = re.compile(r"[A-Z0-9_.-]+")  # Same as camera/protocol.py (no ';').


@dataclass(frozen=True)
class Person:
    x1: float
    y1: float
    x2: float
    y2: float
    score: float = 0.9  # 0..1

    @property
    def area(self):
        return max(0.0, self.x2 - self.x1) * max(0.0, self.y2 - self.y1)


def choose_person(people, multi_ratio=0.5, min_area_px=0):
    """(person or None, multi). Biggest box is the person on the scale; another box
    at least multi_ratio of its area means two people."""
    people = [p for p in people if p.area > 0 and p.area >= min_area_px]
    if not people:
        return None, False
    people.sort(key=lambda p: p.area, reverse=True)
    multi = len(people) > 1 and people[1].area >= multi_ratio * people[0].area
    return people[0], multi


def _norm(value, size):
    return min(1.0, max(0.0, value / size))


def box_label(person, width, height):
    """'BOX_0.400_0.100_0.600_0.950_88' for a pixel box in a width x height image."""
    if width <= 0 or height <= 0:
        raise ValueError("image size must be positive")
    values = (person.x1, person.y1, person.x2, person.y2, person.score)
    if not all(isinstance(v, (int, float)) and math.isfinite(v) for v in values):
        raise ValueError("box values must be finite numbers")
    x1, x2 = _norm(person.x1, width), _norm(person.x2, width)
    y1, y2 = _norm(person.y1, height), _norm(person.y2, height)
    if not (x2 > x1 and y2 > y1):
        raise ValueError("empty box")
    conf = int(round(min(1.0, max(0.0, person.score)) * 100))
    label = f"BOX_{x1:.3f}_{y1:.3f}_{x2:.3f}_{y2:.3f}_{conf}"
    assert LABEL_RE.fullmatch(label)
    return label


def frame_message(people, width, height, multi_ratio=0.5, min_area_px=0):
    """(label, confidence 0..1, person or None) for one frame."""
    person, multi = choose_person(people, multi_ratio, min_area_px)
    if person is None:
        return "NOBODY", 1.0, None
    if multi:
        return "MULTI", 1.0, person
    try:
        return box_label(person, width, height), min(1.0, max(0.0, person.score)), person
    except ValueError:
        return "NOBODY", 1.0, None
