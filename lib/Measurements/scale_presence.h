#pragma once
#include "measurement_state.h"

struct ScaleSample {
    MeasurementView measurement;
    uint32_t sessionId = 0;
    bool present() const { return measurement.hasWeight && measurement.weightKg > 5.0f; }
};

// Physical scale state must survive camera None/Multiple events. Arrival IDs
// distinguish a rapid unload/reload even if the application misses the gap.
class ScalePresence {
public:
    void update(uint32_t now, float kg, bool stable, bool removed = false) {
        const bool wasPresent = snapshot(now).present();
        if (removed) weight_.clearWeight();
        else weight_.weight(now, kg, stable);
        if (!wasPresent && snapshot(now).present()) ++sessionId_;
    }
    ScaleSample snapshot(uint32_t now) const { return {weight_.weightView(now), sessionId_}; }
private:
    MeasurementState weight_;
    uint32_t sessionId_ = 0;
};
