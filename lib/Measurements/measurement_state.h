#pragma once
#include <cmath>
#include <cstdint>
#ifndef MEASUREMENT_CAMERA_TTL_MS
#define MEASUREMENT_CAMERA_TTL_MS 3000
#endif

enum class PersonStatus { None, Measured, NeedsCalibration, Partial, Multiple };

struct MeasurementView {
    PersonStatus status = PersonStatus::None;
    bool hasHeight = false;
    bool hasWeight = false;
    bool weightStable = false;
    bool hasBMI = false;
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

    void camera(uint32_t now, PersonStatus status, float height = 0, float distance = 0) {
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
    }

    void weight(uint32_t now, float kg, bool stable) {
        if (!std::isfinite(kg) || kg <= 0 || kg > 400) {
            clearWeight();
            return;
        }
        weight_ = kg;
        weightAt_ = now;
        weightValid_ = true;
        weightStable_ = stable;
    }

    void clearWeight() { weightValid_ = false; weightStable_ = false; }

    MeasurementView view(uint32_t now) const {
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

private:
    PersonStatus person_ = PersonStatus::None;
    uint32_t cameraAt_ = 0, weightAt_ = 0;
    bool weightValid_ = false, weightStable_ = false;
    float height_ = 0, distance_ = 0, weight_ = 0;
};
