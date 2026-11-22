#pragma once
#include "measurement_state.h"

// Adapted from upstream camera-power-integration@6c5a005. This helper is
// independent of camera state; integrating it into the native BLE callback
// and FSM is a separate change, not implied by importing this header.
struct ScaleSample {
    MeasurementView measurement;
    uint32_t sessionId = 0;
    bool present() const { return measurement.hasWeight && measurement.weightKg > 5.0f; }
};

class ScalePresence {
public:
    void update(uint32_t now, float kg, bool stable, bool removed = false) {
        const bool wasPresent = snapshot(now).present();
        if (removed) weight_.clearWeight(now);
        else weight_.weight(now, kg, stable);
        if (!wasPresent && snapshot(now).present()) ++sessionId_;
    }
    ScaleSample snapshot(uint32_t now) const { return {weight_.weightView(now), sessionId_}; }
private:
    MeasurementState weight_;
    uint32_t sessionId_ = 0;
};
