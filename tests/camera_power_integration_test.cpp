#include <cassert>
#include <limits>
#include "capture_gate.h"
#include "scale_presence.h"

int main() {
    ScalePresence scale;
    CaptureGate gate;
    assert(gate.canSleep());
    scale.update(1, 5, false);
    assert(!scale.snapshot(1).present());
    scale.update(2, 15, false);
    const auto first = scale.snapshot(2);
    assert(first.present() && first.sessionId == 1);
    assert(gate.request(true, first.sessionId));
    assert(!gate.request(true, first.sessionId));
    uint32_t oldCapture;
    assert(gate.beginCapture(oldCapture));
    gate.request(false, first.sessionId);
    assert(!gate.canSleep()); // Cannot lower CPU while HTTP still owns a buffer.
    assert(!gate.finishCapture(oldCapture));
    assert(gate.canSleep());

    // Camera absence must not erase physical presence or block a new arrival.
    MeasurementState camera;
    camera.camera(3, PersonStatus::None);
    assert(scale.snapshot(3).present());
    scale.update(4, 15, true, true);
    scale.update(5, 20, true); // Unload/reload occurred between control polls.
    const auto second = scale.snapshot(5);
    assert(second.sessionId == 2 && second.measurement.weightStable);
    gate.request(true, second.sessionId);
    uint32_t current;
    assert(gate.beginCapture(current));
    assert(gate.finishCapture(current));
    assert(gate.beginInference(current));
    gate.request(false, second.sessionId);
    assert(!gate.canSleep());
    assert(!gate.finishInference(current)); // Discard an AI result after departure.
    assert(gate.canSleep());

    gate.request(true, 3);
    assert(gate.beginCapture(current));
    assert(gate.finishCapture(current));
    assert(gate.beginInference(current));
    gate.request(true, 4); // New person while an older inference is draining.
    assert(!gate.accepts(current));
    assert(!gate.finishInference(current));
    assert(gate.beginCapture(current));
    assert(gate.finishCapture(current));
    assert(gate.beginInference(current));
    assert(gate.finishInference(current));
    assert(!gate.canSleep()); // Active and idle is not Standby.

    scale.update(10, std::numeric_limits<float>::quiet_NaN(), true);
    assert(!scale.snapshot(10).present());
    scale.update(0xfffffff0U, 20, true);
    assert(scale.snapshot(10).present()); // Millisecond rollover.
    assert(!scale.snapshot(30000).present());
}
