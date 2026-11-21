#pragma once
#include <cmath>
#include <cstdint>
#ifndef MEASUREMENT_CAMERA_TTL_MS
#define MEASUREMENT_CAMERA_TTL_MS 3000
#endif
#ifndef MEASUREMENT_HOLD_MS
#define MEASUREMENT_HOLD_MS 0
#endif

enum class PersonStatus { None, Measured, NeedsCalibration, Partial, Multiple };

struct MeasurementView {
    PersonStatus status = PersonStatus::None;
    bool hasHeight = false;
    bool hasWeight = false;
    bool weightStable = false;
    bool hasBMI = false;
    bool held = false;
    float heightCm = 0;
    float distanceM = 0;
    float weightKg = 0;
    float bmi = 0;
};

// Pure state logic; all times are monotonic milliseconds and wrap safely.
class MeasurementState {
public:
    static constexpr uint32_t CAMERA_TTL_MS = MEASUREMENT_CAMERA_TTL_MS;
    static constexpr uint32_t WEIGHT_TTL_MS = 30000;

    explicit MeasurementState(uint32_t holdMs = MEASUREMENT_HOLD_MS) : holdMs_(holdMs) {}

    void camera(uint32_t now, PersonStatus status, float height = 0, float distance = 0) {
        if (status == PersonStatus::Multiple) {
            clearHold();
        }
        if (status == PersonStatus::None || status == PersonStatus::Multiple) {
            clearWeight();
        } else if (person_ == PersonStatus::None || person_ == PersonStatus::Multiple ||
                   now - cameraAt_ > CAMERA_TTL_MS) {
            // A weight received just before the first box may belong to this arrival.
            if (!weightValid_ || now - weightAt_ > 2000) clearWeight();
        }
        person_ = status;
        cameraAt_ = now;
        height_ = height;
        distance_ = distance;
        snapshotHoldIfComplete(now);
    }

    void weight(uint32_t now, float kg, bool stable) {
        if (!std::isfinite(kg) || kg <= 0 || kg > 400) {
            clearWeight();
            return;
        }
        if (!weightValid_ || now - weightAt_ > WEIGHT_TTL_MS) {
            clearHold();
        }
        weight_ = kg;
        weightAt_ = now;
        weightValid_ = true;
        weightStable_ = stable;
        snapshotHoldIfComplete(now);
    }

    void clearWeight() { weightValid_ = false; weightStable_ = false; }
    void clearHold() { hasHeld_ = false; }

    // BLE diagnostics without a camera session; never provides height or BMI.
    MeasurementView weightView(uint32_t now) const {
        MeasurementView out;
        out.hasWeight = weightValid_ && now - weightAt_ <= WEIGHT_TTL_MS;
        out.weightStable = out.hasWeight && weightStable_;
        if (out.hasWeight) out.weightKg = weight_;
        return out;
    }

    MeasurementView view(uint32_t now) const {
        const MeasurementView out = computeLiveView(now);
        if (out.hasBMI) return out;
        if (holdMs_ > 0 && hasHeld_ && (now - heldAt_ <= holdMs_) &&
            out.status != PersonStatus::Multiple && !out.hasWeight) {
            return heldView_;
        }
        return out;
    }

private:
    MeasurementView computeLiveView(uint32_t now) const {
        MeasurementView out;
        if (now - cameraAt_ > CAMERA_TTL_MS) return out;
        out.status = person_;
        if (person_ == PersonStatus::None || person_ == PersonStatus::Multiple) return out;
        out.hasHeight = person_ == PersonStatus::Measured && std::isfinite(height_) &&
                        height_ >= 30 && height_ <= 250 && std::isfinite(distance_) &&
                        distance_ > 0 && distance_ <= 20;
        out.heightCm = height_;
        out.distanceM = distance_;
        out.hasWeight = weightValid_ && now - weightAt_ <= WEIGHT_TTL_MS;
        out.weightStable = out.hasWeight && weightStable_;
        out.weightKg = weight_;
        out.hasBMI = out.hasHeight && out.weightStable;
        if (out.hasBMI) {
            const float metres = height_ / 100.0f;
            out.bmi = weight_ / (metres * metres);
        }
        return out;
    }

    void snapshotHoldIfComplete(uint32_t now) {
        if (holdMs_ == 0) return;
        const MeasurementView live = computeLiveView(now);
        if (live.hasBMI) {
            heldView_ = live;
            heldView_.held = true;
            heldAt_ = now;
            hasHeld_ = true;
        }
    }

    PersonStatus person_ = PersonStatus::None;
    uint32_t cameraAt_ = 0, weightAt_ = 0;
    bool weightValid_ = false, weightStable_ = false;
    float height_ = 0, distance_ = 0, weight_ = 0;
    uint32_t holdMs_ = 0;
    bool hasHeld_ = false;
    uint32_t heldAt_ = 0;
    MeasurementView heldView_;
};
