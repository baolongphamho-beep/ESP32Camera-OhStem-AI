#pragma once
#include <algorithm>
#include <cmath>

struct PersonBox { float x1, y1, x2, y2, score; };

// Match ESP-DL ImagePreprocessor's integer letterbox borders exactly.
inline PersonBox originalBox(PersonBox b, int width, int height, int input = 224) {
    const float scale = std::min(float(input) / width, float(input) / height);
    const int activeW = width >= height ? input : int(scale * width);
    const int activeH = width >= height ? int(scale * height) : input;
    const int left = (input - activeW) / 2, top = (input - activeH) / 2;
    b.x1 = (b.x1 - left) * width / activeW;
    b.x2 = (b.x2 - left) * width / activeW;
    b.y1 = (b.y1 - top) * height / activeH;
    b.y2 = (b.y2 - top) * height / activeH;
    return b;
}

inline bool fullBox(const PersonBox& b, int width, int height) {
    // A box cannot prove full visibility, but reject boxes touching image edges.
    return b.x1 > 2 && b.y1 > 2 && b.x2 < width - 2 && b.y2 < height - 2 &&
           b.x2 > b.x1 && b.y2 - b.y1 >= height * .2f;
}

inline bool groundHeight(const PersonBox& b, int width, int height, float focal,
                         float cameraCm, float tiltDeg, float& cm, float& distance) {
    if (focal <= 0 || cameraCm <= 0 || std::fabs(tiltDeg) >= 80) return false;
    const float theta = tiltDeg * 3.14159265359f / 180;
    const float s = std::sin(theta), c = std::cos(theta);
    const float cy = (height - 1) / 2.f, cx = (width - 1) / 2.f;
    const float feetRay = (cy - b.y2) / focal, headRay = (cy - b.y1) / focal;
    const float fu = feetRay * c - s, ff = feetRay * s + c;
    const float hu = headRay * c - s, hf = headRay * s + c;
    if (fu >= -.001f || ff <= 0 || hf <= 0) return false;
    const float z = -(cameraCm / 100) * ff / fu;
    cm = cameraCm + 100 * z * hu / hf;
    const float x = z * (((b.x1 + b.x2) / 2 - cx) / focal) / ff;
    distance = std::hypot(z, x);
    return std::isfinite(cm) && cm >= 30 && cm <= 250 &&
           std::isfinite(distance) && distance > 0 && distance <= 20;
}
