#pragma once
#include <cstdint>
#include "../../../lib/EdgeSweep/edge_fsm.h"

// A request is issued only after settling. Any move/reset/APPLY invalidates its image.
// Keep busy until the worker replies even after invalidation: never reuse its buffer.
class CaptureGate {
public:
    void invalidate() { ++epoch_; }
    bool canRequest(SystemPhase phase, bool settled, bool cameraTest = false) const {
        return !busy_ && (cameraTest || (settled &&
            (phase == SystemPhase::ServoSweep || phase == SystemPhase::LockedMeasure)));
    }
    uint32_t begin() { busy_ = true; return epoch_; }
    bool current(uint32_t epoch) const { return epoch == epoch_; }
    bool finish(uint32_t epoch) { busy_ = false; return current(epoch); }
private:
    bool busy_ = false;
    uint32_t epoch_ = 0;
};
