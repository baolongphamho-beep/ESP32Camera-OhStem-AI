// g++ -std=c++17 -I lib/Measurements tests/measurement_state_test.cpp -o .pio/mst.exe; .pio/mst.exe
#include <cassert>
#include <cmath>
#include <cstdio>
#include "measurement_state.h"
#include "camera_packet.h"

static constexpr uint32_t HOLD = 12000;  // hold_sec = 12

// Person measured: H 180 cm, D 3 m, W 81 kg locked -> BMI 25.
static void measure(MeasurementState& s, uint32_t t) {
    s.camera(t, PersonStatus::Measured, 180, 3);
    s.weight(t + 100, 81, true);
    assert(s.view(t + 100).hasBMI);
}

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void testResultHold() {
    // 0) Default (MEASUREMENT_RESULT_HOLD_MS = 0): old behaviour, nothing held.
    {
        MeasurementState s;
        assert(s.resultHoldMs() == 0);
        measure(s, 1000);
        s.clearWeight(1500);
        assert(!s.view(1500).hasBMI && !s.heldResult(1500).active);
    }
    // 1) Still standing on the scale: live BMI, no hold reported.
    {
        MeasurementState s;
        s.setResultHoldMs(HOLD);
        measure(s, 1000);
        assert(!s.heldResult(1200).active);
        assert(s.view(1200).hasBMI);
    }
    // 2) load_removed BEFORE a result (weight never locked): nothing to hold.
    {
        MeasurementState s;
        s.setResultHoldMs(HOLD);
        s.camera(1000, PersonStatus::Measured, 180, 3);
        s.weight(1100, 81, false);
        s.clearWeight(1500);
        assert(!s.heldResult(1500).active);
    }
    // 3) load_removed AFTER H/W/BMI: result kept, presence (hasWeight) is off.
    {
        MeasurementState s;
        s.setResultHoldMs(HOLD);
        measure(s, 1000);
        s.clearWeight(2000);
        const MeasurementView v = s.view(2000);
        assert(!v.hasWeight && !v.hasBMI);  // Live state unchanged: person is gone.
        ResultHold h = s.heldResult(2000);
        assert(h.active && near(h.heightCm, 180) && near(h.weightKg, 81) &&
               near(h.bmi, 25) && near(h.distanceM, 3) && h.remainingMs == HOLD);
        // 4) Still there during the hold; remaining time counts down.
        h = s.heldResult(2000 + 5000);
        assert(h.active && h.remainingMs == HOLD - 5000);
        assert(s.heldResult(2000 + HOLD).active);       // Last millisecond.
        // 5) Reset only after the timeout.
        assert(!s.heldResult(2000 + HOLD + 1).active);
        // Camera "nobody" after stepping off does not cut the hold short.
        MeasurementState c;
        c.setResultHoldMs(HOLD);
        measure(c, 1000);
        c.clearWeight(2000);
        c.camera(2500, PersonStatus::None);
        assert(c.heldResult(2500).active && c.heldResult(2500).remainingMs == HOLD - 500);
    }
    // 6) New person steps on during the hold: old result disappears at once.
    {
        MeasurementState s;
        s.setResultHoldMs(HOLD);
        measure(s, 1000);
        s.clearWeight(2000);
        s.camera(3000, PersonStatus::Partial);
        assert(s.heldResult(3000).active);
        s.weight(4000, 55, false);                       // New load.
        assert(!s.heldResult(4000).active);
        assert(s.view(4000).hasWeight && near(s.view(4000).weightKg, 55));
        // ...and their own result is held next time, not the old one.
        s.camera(4500, PersonStatus::Measured, 160, 3);
        s.weight(4600, 64, true);
        s.clearWeight(5000);
        const ResultHold h = s.heldResult(5000);
        assert(h.active && near(h.heightCm, 160) && near(h.weightKg, 64) && near(h.bmi, 25));
    }
    // 7) Person walks out of the camera first (still on scale): hold starts then;
    //    their own weight packets (no load_removed) do not cancel it.
    {
        MeasurementState s;
        s.setResultHoldMs(HOLD);
        measure(s, 1000);
        s.camera(2000, PersonStatus::None);
        assert(s.heldResult(2000).active);
        s.weight(2500, 81, true);
        assert(s.heldResult(2500).active);
        s.clearWeight(3000);                             // Steps off later.
        assert(s.heldResult(3000).active && s.heldResult(3000).remainingMs == HOLD - 1000);
    }
    // 8) A new live BMI ends the hold (live data always wins).
    {
        MeasurementState s;
        s.setResultHoldMs(HOLD);
        measure(s, 1000);
        s.camera(2000, PersonStatus::Multiple);
        assert(s.heldResult(2000).active);
        measure(s, 3000);
        assert(!s.heldResult(3100).active && s.view(3100).hasBMI);
    }
    // 9) BMI that only expired by camera TTL (no call): hold counts from the last call.
    {
        MeasurementState s;
        s.setResultHoldMs(HOLD);
        measure(s, 1000);                                // Last call at 1100.
        const uint32_t late = 1100 + MeasurementState::CAMERA_TTL_MS + 1;
        assert(!s.view(late).hasBMI);
        assert(s.heldResult(late).active);
        assert(!s.heldResult(1100 + HOLD + 1).active);
    }
    // 10) Turning hold off, and an explicit reset, drop the held result.
    {
        MeasurementState s;
        s.setResultHoldMs(HOLD);
        measure(s, 1000);
        s.clearWeight(2000);
        s.clearResultHold();
        assert(!s.heldResult(2000).active);
        measure(s, 3000);
        s.clearWeight(4000);
        s.setResultHoldMs(0);
        assert(!s.heldResult(4000).active);
    }
    // 11) clearWeight() without a time (old API used by measurements.cpp) still works.
    {
        MeasurementState s;
        s.setResultHoldMs(HOLD);
        measure(s, 1000);
        s.clearWeight();
        assert(s.heldResult(1200).active && !s.view(1200).hasWeight);
    }
    // 12) millis() rollover during the hold.
    {
        MeasurementState s;
        s.setResultHoldMs(HOLD);
        measure(s, 0xFFFFE000U);
        s.clearWeight(0xFFFFE800U);                      // Steps off 6144 ms before the wrap.
        assert(s.heldResult(0x00001000U).active);        // 10240 ms later, across the wrap.
        assert(!s.heldResult(0xFFFFE800U + HOLD + 1).active);
    }
}

int main() {
    MeasurementState state;
    assert(state.view(0).status == PersonStatus::None);
    state.camera(1000, PersonStatus::Measured, 180, 3);
    assert(state.view(1000).hasHeight);
    assert(!state.view(1000).hasBMI);
    state.weight(1100, 81, false);
    assert(!state.view(1100).hasBMI);  // Provisional weight is not a BMI result.
    state.weight(1200, 81, true);
    assert(std::fabs(state.view(1200).bmi - 25) < 0.001f);
    state.camera(1300, PersonStatus::None);
    assert(!state.view(1300).hasHeight && !state.view(1300).hasBMI);
    state.camera(1400, PersonStatus::Measured, 160, 3);
    assert(!state.view(1400).hasWeight);  // No weight carried across absence.
    state.weight(1500, 64, true);
    state.camera(1600, PersonStatus::Partial);
    assert(!state.view(1600).hasBMI);
    state.camera(1700, PersonStatus::Measured, 160, 3);
    assert(state.view(1700).hasBMI);
    assert(!state.view(4800).hasBMI);  // Camera TTL.
    state.camera(5000, PersonStatus::Measured, 170, 3);
    assert(!state.view(5000).hasWeight); // New camera session, old weight discarded.
    state.weight(5100, 80, true);
    state.camera(5200, PersonStatus::Multiple);
    assert(!state.view(5200).hasWeight);
    state.camera(36000, PersonStatus::Measured, 170, 3);
    assert(!state.view(36000).hasBMI);
    state.weight(36001, 80, true);
    state.clearWeight();
    assert(!state.view(36002).hasWeight);

    // Millisecond counter rollover must not expire a newly received packet.
    state.camera(0xfffffff0U, PersonStatus::Measured, 180, 3);
    state.weight(0xfffffff5U, 81, true);
    assert(state.view(20).hasBMI);

    PersonStatus status;
    float height, distance;
    assert(decodeCameraLabel("MEAS_H180.0_D3.000", status, height, distance));
    assert(status == PersonStatus::Measured && height == 180 && distance == 3);
    assert(decodeCameraLabel("NO_PEOPLE", status, height, distance));
    assert(status == PersonStatus::None);
    assert(!decodeCameraLabel("MEAS_H180_Dnan", status, height, distance));
    assert(!decodeCameraLabel("MEAS_H180_D3junk", status, height, distance));
    assert(!decodeCameraLabel("MEAS_H0_D3", status, height, distance));
    assert(!decodeCameraLabel("Face", status, height, distance));

    testResultHold();
    std::puts("measurement_state_test: OK");
    return 0;
}
