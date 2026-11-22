#pragma once
#include <cmath>
#include <cstdint>
#ifndef MEASUREMENT_CAMERA_TTL_MS
#define MEASUREMENT_CAMERA_TTL_MS 3000
#endif
// Result hold (Plan 2, TV5): keep the last H/W/BMI this long after it stops being
// live (person stepped off), so it can still be read on the LCD. 0 = off, which
// keeps the old behaviour. Runtime value: setResultHoldMs(hold_sec * 1000).
#ifndef MEASUREMENT_RESULT_HOLD_MS
#define MEASUREMENT_RESULT_HOLD_MS 0
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

// Last complete result, kept for a while after the person steps off.
// Separate from MeasurementView on purpose: view().hasWeight stays "someone is
// on the scale now", which callers use as presence.
struct ResultHold {
    bool active = false;
    float heightCm = 0;
    float distanceM = 0;
    float weightKg = 0;
    float bmi = 0;
    uint32_t remainingMs = 0;
};

// Pure state logic; all times are monotonic milliseconds and wrap safely.
class MeasurementState {
public:
    static constexpr uint32_t CAMERA_TTL_MS = MEASUREMENT_CAMERA_TTL_MS;
    static constexpr uint32_t WEIGHT_TTL_MS = 30000;

    void camera(uint32_t now, PersonStatus status, float height = 0, float distance = 0) {
        const bool hadBMI = view(now).hasBMI;
        if (status == PersonStatus::None || status == PersonStatus::Multiple) {
            dropWeight();
        } else if (person_ == PersonStatus::None || person_ == PersonStatus::Multiple ||
                   now - cameraAt_ > CAMERA_TTL_MS) {
            // A weight received just before the first box may belong to this arrival.
            if (!weightValid_ || now - weightAt_ > 2000) dropWeight();
        }
        person_ = status;
        cameraAt_ = now;
        height_ = height;
        distance_ = distance;
        afterChange(now, hadBMI);
    }

    void weight(uint32_t now, float kg, bool stable) {
        const bool hadBMI = view(now).hasBMI;
        if (!std::isfinite(kg) || kg <= 0 || kg > 400) {
            dropWeight();
            afterChange(now, hadBMI);
            return;
        }
        // A load after the scale reported "load removed" is a new person: the
        // previous person's held result must not stay on screen.
        if (loadRemoved_) {
            hasResult_ = false;
            loadRemoved_ = false;
        }
        weight_ = kg;
        weightAt_ = now;
        weightValid_ = true;
        weightStable_ = stable;
        afterChange(now, hadBMI);
    }

    // Called by the scale driver on "load removed", weight 0 or disconnect.
    void clearWeight() { clearWeight(lastNow_); }
    void clearWeight(uint32_t now) {
        const bool hadBMI = view(now).hasBMI;
        dropWeight();
        loadRemoved_ = true;
        afterChange(now, hadBMI);
    }

    void setResultHoldMs(uint32_t ms) {
        holdMs_ = ms;
        if (!ms) hasResult_ = false;
    }
    uint32_t resultHoldMs() const { return holdMs_; }
    // smartscale-edge-ai names (BaoLong 22/11), same behaviour.
    void setHoldMs(uint32_t ms) { setResultHoldMs(ms); }
    uint32_t holdMs() const { return holdMs_; }
    // Drop any held result (e.g. a "reset" command).
    void clearResultHold() { hasResult_ = false; }

    // Raw BLE diagnostics remain usable before any camera result exists.
    // Keep this separate from view(): physical presence must not imply a BMI.
    MeasurementView weightView(uint32_t now) const {
        MeasurementView out;
        out.hasWeight = weightValid_ && now - weightAt_ <= WEIGHT_TTL_MS;
        out.weightStable = out.hasWeight && weightStable_;
        if (out.hasWeight) out.weightKg = weight_;
        return out;
    }

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

    // Held result while there is no live BMI. Inactive when hold is off, when a
    // live BMI exists, after holdMs, or once a new person loads the scale.
    // If the live BMI only expired by TTL (no call), the hold counts from the
    // last call that still had a BMI, so it may end up to one TTL earlier.
    ResultHold heldResult(uint32_t now) const {
        ResultHold out;
        if (!holdMs_ || !hasResult_ || view(now).hasBMI) return out;
        const uint32_t age = now - resultAt_;
        if (age > holdMs_) return out;
        out.active = true;
        out.heightCm = result_.heightCm;
        out.distanceM = result_.distanceM;
        out.weightKg = result_.weightKg;
        out.bmi = result_.bmi;
        out.remainingMs = holdMs_ - age;
        return out;
    }

private:
    void dropWeight() { weightValid_ = false; weightStable_ = false; }

    // Remember the latest complete result; when a call ends a live BMI, the
    // hold starts now.
    void afterChange(uint32_t now, bool hadBMI) {
        lastNow_ = now;
        const MeasurementView v = view(now);
        if (v.hasBMI) {
            result_ = v;
            resultAt_ = now;
            hasResult_ = holdMs_ != 0;
        } else if (hadBMI && hasResult_) {
            resultAt_ = now;
        } else if (hasResult_ && now - resultAt_ > holdMs_) {
            hasResult_ = false;  // Expired; never revive it after millis() wraps.
        }
    }

    PersonStatus person_ = PersonStatus::None;
    uint32_t cameraAt_ = 0, weightAt_ = 0, lastNow_ = 0;
    bool weightValid_ = false, weightStable_ = false;
    float height_ = 0, distance_ = 0, weight_ = 0;

    uint32_t holdMs_ = MEASUREMENT_RESULT_HOLD_MS, resultAt_ = 0;
    bool hasResult_ = false, loadRemoved_ = false;
    MeasurementView result_;
};
