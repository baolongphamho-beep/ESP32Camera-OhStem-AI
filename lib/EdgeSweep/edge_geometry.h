#pragma once
// Plan 2: framing of the person box + height from the servo tilt. Pure C++.
//
//   tilt_down = servo_dir * (servo_command - servo_level)          (deg, + = looking down)
//               servo_dir = -1: larger command tilts UP, +1: DOWN (TV3 servo_motion.h convention)
//   a(v)      = tilt_down + atan((v - 0.5) / f)                    ray below horizontal for image row v
//   D         = h_c / tan(a(v_feet))                               feet are on the scale top
//   H         = h_c - D * tan(a(v_head))                           head above the scale top
// v = row / image height (0 top, 1 bottom), f = fy / image height, h_c = lens above scale (cm).
// Needs both feet and head in the image: that is why the servo sweeps until "FullBody".
#include <cmath>

enum class FramingStatus { None, FullBody, TopClipped, BottomClipped, TooClose, SideClipped };

inline const char* framingName(FramingStatus s) {
    switch (s) {
        case FramingStatus::None: return "None";
        case FramingStatus::FullBody: return "FullBody";
        case FramingStatus::TopClipped: return "TopClipped";
        case FramingStatus::BottomClipped: return "BottomClipped";
        case FramingStatus::TooClose: return "TooClose";
        case FramingStatus::SideClipped: return "SideClipped";
    }
    return "None";
}

struct PersonBox {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // pixels (or any unit matching width/height)
    float score = 0;
};

// Interface contract signature (Plan 2, TV4).
inline FramingStatus analyzeFraming(const PersonBox& box, int width, int height, float marginPx) {
    if (width <= 0 || height <= 0 || !(box.x2 > box.x1) || !(box.y2 > box.y1)) return FramingStatus::None;
    const bool top = box.y1 <= marginPx;
    const bool bottom = box.y2 >= (float)height - marginPx;
    const bool side = box.x1 <= marginPx || box.x2 >= (float)width - marginPx;
    if (top && bottom) return FramingStatus::TooClose;  // No angle shows head AND feet.
    if (top) return FramingStatus::TopClipped;
    if (bottom) return FramingStatus::BottomClipped;
    if (side) return FramingStatus::SideClipped;
    return FramingStatus::FullBody;
}

namespace edge_geometry {
constexpr float kPi = 3.14159265358979f;
inline float deg(float rad) { return rad * 180.0f / kPi; }
inline float rad(float d) { return d * kPi / 180.0f; }

inline float tiltDownDeg(float servoDeg, float levelDeg, int dir) {
    return (dir < 0 ? -1.0f : 1.0f) * (servoDeg - levelDeg);  // == servoTiltFromDeg()
}

// Ray angle below horizontal (deg) for image row v (0..1).
inline float rayDownDeg(float v, float tiltDown, float focal) {
    return tiltDown + deg(std::atan((v - 0.5f) / focal));
}

struct HeightResult {
    bool valid = false;
    float heightCm = 0;
    float distanceM = 0;
};

inline HeightResult heightFromBox(float vTop, float vBottom, float tiltDown, float cameraHeightCm,
                                  float focal) {
    HeightResult out;
    if (!std::isfinite(vTop) || !std::isfinite(vBottom) || !std::isfinite(tiltDown) ||
        !(vBottom > vTop) || vTop < 0 || vBottom > 1 || !(focal > 0) || !(cameraHeightCm > 0)) {
        return out;
    }
    const float feet = rayDownDeg(vBottom, tiltDown, focal);
    const float head = rayDownDeg(vTop, tiltDown, focal);
    if (feet < 0.5f || feet > 89.0f || std::fabs(head) > 89.0f) return out;  // Feet ray must hit the scale.
    const float distanceCm = cameraHeightCm / std::tan(rad(feet));
    out.heightCm = cameraHeightCm - distanceCm * std::tan(rad(head));
    out.distanceM = distanceCm / 100.0f;
    out.valid = std::isfinite(out.heightCm) && out.heightCm >= 30 && out.heightCm <= 250 &&
                out.distanceM >= 0.2f && out.distanceM <= 20;
    return out;
}

// Median of up to 8 values (small n, insertion sort).
inline float median(const float* values, int n) {
    float v[8];
    if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) {
        float x = values[i];
        int j = i - 1;
        while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; --j; }
        v[j + 1] = x;
    }
    if (n <= 0) return 0;
    return n % 2 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}
}  // namespace edge_geometry
