"""Pinhole approximations. Camera upright, roll=0; feet on the scale surface."""
import math


def positive(value, name):
    if value is None or not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be a positive finite number")
    return float(value)


def calibrate_focal(pixel_height, distance_m, reference_cm):
    """Level camera, full reference person at known optical-forward distance."""
    return positive(pixel_height, "box height") * positive(distance_m, "distance") / (positive(reference_cm, "reference height") / 100)


def fixed_distance_height(pixel_height, focal_px, distance_m):
    height = 100 * positive(pixel_height, "box height") * positive(distance_m, "distance") / positive(focal_px, "focal_px")
    return valid_result(height, distance_m)


def ground_plane_height(box, resolution, focal_px, camera_height_cm, tilt_deg=0):
    """Foot ray intersects scale-top plane; head lies above foot at same depth.

    fy in pixels, fx=fy, principal point at image centre. Positive tilt is DOWN.
    Distance is horizontal camera-foot range. Lens distortion is not corrected.
    """
    focal_px = positive(focal_px, "focal_px")
    camera_h = positive(camera_height_cm, "camera height") / 100
    if not math.isfinite(tilt_deg) or abs(tilt_deg) >= 80:
        raise ValueError("tilt must be between -80 and 80 degrees")
    x1, y1, x2, y2 = box
    width, height = resolution
    if not (0 <= x1 < x2 <= width and 0 <= y1 < y2 <= height):
        raise ValueError("invalid bounding box")
    cx, cy = (width - 1) / 2, (height - 1) / 2
    theta = math.radians(tilt_deg)
    sine, cosine = math.sin(theta), math.cos(theta)

    def ray(y):
        up = (cy - y) / focal_px
        return up * cosine - sine, up * sine + cosine

    feet_up, feet_forward = ray(y2)
    head_up, head_forward = ray(y1)
    if feet_up >= -0.001 or feet_forward <= 0 or head_forward <= 0:
        raise ValueError("feet ray must point below horizon; check camera height/tilt")
    z = -camera_h * feet_forward / feet_up
    measured_height = 100 * (camera_h + z * head_up / head_forward)
    x_ray = (((x1 + x2) / 2) - cx) / focal_px
    x_world = z * x_ray / feet_forward
    return valid_result(measured_height, math.hypot(z, x_world))


def valid_result(height_cm, distance_m):
    if not math.isfinite(height_cm) or not 30 <= height_cm <= 250:
        raise ValueError("height outside 30..250 cm; check geometry/calibration")
    if not math.isfinite(distance_m) or not 0 < distance_m <= 20:
        raise ValueError("distance outside 0..20 m; check geometry/calibration")
    return height_cm, distance_m


def measurement_label(height_cm, distance_m):
    valid_result(height_cm, distance_m)
    return f"MEAS_H{height_cm:.1f}_D{distance_m:.3f}"
