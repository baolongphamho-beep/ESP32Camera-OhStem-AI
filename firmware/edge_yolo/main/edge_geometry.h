#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

struct PersonBox { float x1, y1, x2, y2, score; };

enum class FramingStatus {
    None,
    FullBody,
    TopClipped,
    BottomClipped,
    TooClose,
    SideClipped,
};

inline const char* framingStatusName(FramingStatus status) {
    switch (status) {
        case FramingStatus::None: return "NONE";
        case FramingStatus::FullBody: return "FULL_BODY";
        case FramingStatus::TopClipped: return "TOP_CLIPPED";
        case FramingStatus::BottomClipped: return "BOTTOM_CLIPPED";
        case FramingStatus::TooClose: return "TOO_CLOSE";
        case FramingStatus::SideClipped: return "SIDE_CLIPPED";
    }
    return "NONE";
}

// Match ESP-DL ImagePreprocessor's integer letterbox borders exactly.
inline PersonBox originalBox(PersonBox b, int width, int height, int input = 224) {
    if (width <= 0 || height <= 0 || input <= 0) return {0, 0, 0, 0, b.score};
    const float scale = std::min(float(input) / width, float(input) / height);
    const int activeW = std::max(1, width >= height ? input : int(scale * width));
    const int activeH = std::max(1, width >= height ? int(scale * height) : input);
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

inline FramingStatus analyzeFraming(const PersonBox& box,
                                    int width,
                                    int height,
                                    float marginPx = 8.0f,
                                    float minHeightRatio = 0.20f) {
    if (width <= 0 || height <= 0 || !std::isfinite(marginPx) || marginPx < 0.0f ||
        !std::isfinite(minHeightRatio) || minHeightRatio < 0.0f) {
        return FramingStatus::None;
    }
    if (!std::isfinite(box.x1) || !std::isfinite(box.y1) ||
        !std::isfinite(box.x2) || !std::isfinite(box.y2)) {
        return FramingStatus::None;
    }
    const float bw = box.x2 - box.x1;
    const float bh = box.y2 - box.y1;
    if (bw < 2.0f || bh < 2.0f) {
        return FramingStatus::None;
    }
    const bool topClipped = (box.y1 <= marginPx);
    const bool bottomClipped = (box.y2 >= static_cast<float>(height) - marginPx);
    const bool sideClipped = (box.x1 <= marginPx) || (box.x2 >= static_cast<float>(width) - marginPx);
    if (topClipped && bottomClipped) return FramingStatus::TooClose;
    if (topClipped) return FramingStatus::TopClipped;
    if (bottomClipped) return FramingStatus::BottomClipped;
    if (sideClipped) return FramingStatus::SideClipped;
    if (bh < static_cast<float>(height) * minHeightRatio) {
        return FramingStatus::None;
    }
    return FramingStatus::FullBody;
}

// focal is pixel focal length (cfg.focalPx * height), NOT normalized fy/H.
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

// focal is pixel focal length (cfg.focalPx * height), NOT normalized fy/H.
// outTiltDeg is intentionally unclamped so groundHeight() rejects |tilt| >= 80 deg.
inline bool groundHeightAtServo(const PersonBox& b,
                                int width,
                                int height,
                                float focal,
                                float cameraCm,
                                float servoDeg,
                                float servoLevelDeg,
                                int8_t servoDir,
                                float tiltOffsetDeg,
                                float& cm,
                                float& distance,
                                float& outTiltDeg) {
    if (!std::isfinite(servoDeg) || !std::isfinite(servoLevelDeg) || !std::isfinite(tiltOffsetDeg)) {
        return false;
    }
    const float sign = (servoDir >= 0) ? 1.0f : -1.0f;
    outTiltDeg = tiltOffsetDeg + sign * (servoDeg - servoLevelDeg);
    return groundHeight(b, width, height, focal, cameraCm, outTiltDeg, cm, distance);
}

class HeightMedianFilter {
public:
    static constexpr size_t kMaxSamples = 5;

    void reset() {
        count_ = 0;
        next_ = 0;
    }

    void push(float cm, float dist) {
        if (!std::isfinite(cm) || !std::isfinite(dist)) return;
        cm_[next_] = cm;
        dist_[next_] = dist;
        next_ = (next_ + 1) % kMaxSamples;
        if (count_ < kMaxSamples) ++count_;
    }

    bool median(float& outCm, float& outDist) const {
        if (count_ == 0) return false;
        float sortedCm[kMaxSamples];
        float sortedDist[kMaxSamples];
        for (size_t i = 0; i < count_; ++i) {
            sortedCm[i] = cm_[i];
            sortedDist[i] = dist_[i];
        }
        std::sort(sortedCm, sortedCm + count_);
        std::sort(sortedDist, sortedDist + count_);
        const size_t mid = count_ / 2;
        if (count_ % 2 == 1) {
            outCm = sortedCm[mid];
            outDist = sortedDist[mid];
        } else {
            outCm = 0.5f * (sortedCm[mid - 1] + sortedCm[mid]);
            outDist = 0.5f * (sortedDist[mid - 1] + sortedDist[mid]);
        }
        return true;
    }

    size_t count() const { return count_; }

private:
    float cm_[kMaxSamples] = {};
    float dist_[kMaxSamples] = {};
    size_t count_ = 0;
    size_t next_ = 0;
};

class AutoCalibCollector {
public:
    static constexpr size_t kRequired = 3;

    void reset() {
        count_ = 0;
        next_ = 0;
    }

    bool add(float boxHeightPx, float& outMedianBoxH) {
        if (!std::isfinite(boxHeightPx) || boxHeightPx <= 0.f) return false;
        samples_[next_] = boxHeightPx;
        next_ = (next_ + 1) % kRequired;
        if (count_ < kRequired) ++count_;
        if (count_ < kRequired) return false;

        float sorted[kRequired];
        for (size_t i = 0; i < kRequired; ++i) sorted[i] = samples_[i];
        std::sort(sorted, sorted + kRequired);
        const float minH = sorted[0];
        const float medH = sorted[1];
        const float maxH = sorted[2];
        if (medH > 0.f && (maxH - minH) <= medH * 0.05f) {
            outMedianBoxH = medH;
            return true;
        }
        return false;
    }

    size_t count() const { return count_; }

private:
    float samples_[kRequired] = {};
    size_t count_ = 0;
    size_t next_ = 0;
};
