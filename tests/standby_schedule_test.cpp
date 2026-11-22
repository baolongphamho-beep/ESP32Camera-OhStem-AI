#include <cassert>
#include "standby_schedule.h"

int main() {
    // STANDBY with USB unplugged waits indefinitely for BLE/config wakeup.
    ControlScheduleInput in;
    in.nowMs = 5000;
    in.lastReportAtMs = 5000;
    assert(computeControlWaitMs(in) == kControlWaitForeverMs);

    // STANDBY with USB connected wakes on the 1 s telemetry cadence.
    in.usbConnected = true;
    in.lastReportAtMs = 4200;
    assert(computeControlWaitMs(in) == 200);
    in.lastReportAtMs = 3000; // Overdue telemetry clamps to 1 ms minimum wait.
    assert(computeControlWaitMs(in) == 1);

    // RESULT_HOLD with USB unplugged sleeps until hold expiry (or returns 0 when due).
    in = {};
    in.holding = true;
    in.holdAtMs = 1000;
    in.holdDurationMs = 10000;
    in.nowMs = 3500;
    in.lastReportAtMs = 3500;
    assert(computeControlWaitMs(in) == 7500);
    in.nowMs = 11000;
    assert(computeControlWaitMs(in) == 0);

    // RESULT_HOLD with USB connected wakes at whichever comes first: telemetry or hold expiry.
    in.usbConnected = true;
    in.nowMs = 3200;
    in.lastReportAtMs = 3000;
    assert(computeControlWaitMs(in) == 800);
    in.nowMs = 10700;
    in.lastReportAtMs = 10500;
    assert(computeControlWaitMs(in) == 300);

    // DRAINING polls quickly while waiting for workers to release buffers.
    in = {};
    in.powerActive = true;
    in.nowMs = 2000;
    in.lastReportAtMs = 2000;
    assert(computeControlWaitMs(in) == kControlDrainingPollMs);
    in.usbConnected = true;
    in.nowMs = 2950;
    assert(computeControlWaitMs(in) == 50);

    // ACTIVE caps wait to kControlActivePollMs and still respects earlier telemetry.
    in = {};
    in.active = true;
    in.powerActive = true;
    in.nowMs = 1000;
    in.lastReportAtMs = 1000;
    assert(computeControlWaitMs(in) == kControlActivePollMs);
    in.usbConnected = true;
    in.nowMs = 1850;
    assert(computeControlWaitMs(in) == 150);

    // Millisecond rollover across UINT32_MAX.
    in = {};
    in.holding = true;
    in.holdAtMs = 0xFFFFFF00U;
    in.holdDurationMs = 10000;
    in.nowMs = 0x00000100U; // Elapsed 0x200 = 512 ms.
    assert(computeControlWaitMs(in) == 9488);

    in = {};
    in.usbConnected = true;
    in.lastReportAtMs = 0xFFFFFE00U;
    in.nowMs = 0x00000050U; // Elapsed 0x250 = 592 ms.
    assert(computeControlWaitMs(in) == 408);
    return 0;
}
