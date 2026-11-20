// TV5-5: prints a serial log in the exact format of src/app_main.cpp for a simulated
// QA session (one boot, the 6 Plan 2 sign-off situations, 18 cycles), so that
// tools/qa_log_check.py can be tested without the board.
//   g++ -std=c++17 -I lib/EdgeSweep -I tests tests/qa_log_sim.cpp -o .pio/qals.exe
//   .pio/qals.exe > .pio/qa_sim.log; py -3 tools/qa_log_check.py .pio/qa_sim.log
#include <cassert>
#include <cstdio>

#include "edge_io.h"
#include "edge_sweep_sim.h"

static void logHook(const Sim& s) {
    static bool lastPresent = false, printed = false;
    static SystemPhase lastPhase = SystemPhase::Standby;
    static EdgeNotice lastNotice = EdgeNotice::None;
    const EdgeFsm& f = s.fsm;
    const unsigned long ms = s.now;
    const bool stable = s.onScale && s.now - s.onScaleAt >= s.stableAfterMs;

    LcdData d;
    d.hasHeight = f.hasHeight();
    d.heightCm = f.heightCm();
    d.distanceM = f.distanceM();
    d.hasWeight = s.onScale;
    d.weightKg = s.kg;
    d.weightStable = stable;
    d.hasBmi = f.hasHeight() && stable;
    d.bmi = d.hasBmi ? s.kg / ((f.heightCm() / 100) * (f.heightCm() / 100)) : 0;
    const LcdLines lines = lcdTextFor(f.phase(), f.notice(), d);

    if (f.present() != lastPresent) {
        lastPresent = f.present();
        std::printf("[SCALE] %s kg=%.2f ms=%lu\n", lastPresent ? "on" : "off", s.onScale ? s.kg : 0.0f, ms);
    }
    if (f.phase() != lastPhase || f.notice() != lastNotice) {
        lastPhase = f.phase();
        lastNotice = f.notice();
        std::printf("[SWEEP] -> %s%s%s servo=%.1f tilt=%.1f ms=%lu | LCD \"%s\" / \"%s\"\n",
                    phaseName(lastPhase), lastNotice != EdgeNotice::None ? " notice=" : "",
                    noticeName(lastNotice), f.servoCommand(), f.tiltDeg(), ms, lines.line[0], lines.line[1]);
    }
    if (d.hasBmi && f.phase() == SystemPhase::ResultHold && !printed) {
        std::printf("[RESULT] H=%.1f W=%.2f BMI=%.2f D=%.2f servo=%.1f ms=%lu\n", d.heightCm, d.weightKg,
                    d.bmi, d.distanceM, f.servoCommand(), ms);
        std::printf("========== HEIGHT / WEIGHT / BMI ==========\n");
    }
    printed = d.hasBmi && f.phase() == SystemPhase::ResultHold;
}

static void measure(Sim& s, float head, float dist, float kg) {
    s.kg = kg;
    s.stepOn(head, dist);
    assert(s.until(SystemPhase::ResultHold, 60000) >= 0);
    s.run(2000);  // Reads the LCD while still on the scale.
    s.stepOff();
    assert(s.until(SystemPhase::Standby, 20000) >= 0);
    s.run(1500);
}

int main() {
    std::printf("ESP-ROM:esp32s3-20210327\n[BOOT] Plan 2 servo sweep + scale + BMI\n");
    std::printf("[SYSTEM] Ready (STANDBY: step on the scale)\n");
    Sim s(EdgeConfig(), 5000);
    s.afterTick = logHook;
    s.run(1000);

    // QA2: several heights (and QA5 hold, QA6 count).
    const float heads[] = {120, 135, 150, 160, 165, 170, 175, 180, 185, 190};
    for (int i = 0; i < 10; ++i) measure(s, heads[i], 280 + (i % 3) * 20, 30 + i * 6);

    // QA3a: too close -> "Step back!", then steps back.
    s.kg = 70;
    s.stepOn(175, 120);
    assert(s.untilNotice(EdgeNotice::TooClose, 60000) >= 0);
    s.run(1000);
    s.distCm = 300;
    assert(s.until(SystemPhase::ResultHold, 60000) >= 0);
    s.run(1000);
    s.stepOff();
    assert(s.until(SystemPhase::Standby, 20000) >= 0);
    s.run(1500);

    // QA3b: two people -> "One at a time".
    s.stepOn(168, 290);
    assert(s.until(SystemPhase::ServoSweep, 5000) >= 0);
    s.run(1500);
    s.multi = true;
    s.run(4000);
    s.multi = false;
    assert(s.until(SystemPhase::ResultHold, 60000) >= 0);
    s.stepOff();
    assert(s.until(SystemPhase::Standby, 20000) >= 0);
    s.run(1500);

    // QA4: step off in the middle of the sweep.
    s.stepOn(172, 300);
    assert(s.until(SystemPhase::ServoSweep, 5000) >= 0);
    s.run(1200);
    s.stepOff();
    assert(s.until(SystemPhase::Standby, 5000) >= 0);
    s.run(1500);

    // Next person during RESULT_HOLD (replaces the shown result).
    s.stepOn(158, 290);
    assert(s.until(SystemPhase::ResultHold, 60000) >= 0);
    s.stepOff();
    s.run(4000);
    s.stepOn(182, 300);
    assert(s.until(SystemPhase::WakingHoming, 500) >= 0);

    // More normal cycles to reach >= 15 for QA6.
    assert(s.until(SystemPhase::ResultHold, 60000) >= 0);
    s.stepOff();
    assert(s.until(SystemPhase::Standby, 20000) >= 0);
    s.run(1500);
    measure(s, 145, 300, 42);
    measure(s, 178, 320, 80);
    return 0;
}
