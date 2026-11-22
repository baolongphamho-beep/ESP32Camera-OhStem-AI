// g++ -std=c++17 -I lib/EdgeSweep -I tests tests/edge_sweep_fsm_test.cpp -o .pio/sft.exe; .pio/sft.exe
// Simulates scale + servo + camera + laptop (tests/edge_sweep_sim.h) for the Plan 2 sweep:
// STANDBY -> ... -> RESULT_HOLD -> STANDBY.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "edge_sweep_sim.h"

int main() {
    // 1) Full pipeline for several people: wake, sweep up, stop on full body, height, hold.
    for (float head : {120.0f, 150.0f, 170.0f, 190.0f}) {
        for (float dist : {280.0f, 320.0f}) {
            Sim s;
            assert(s.fsm.phase() == SystemPhase::Standby && !s.fsm.servoActive());
            s.run(1000);
            assert(s.fsm.phase() == SystemPhase::Standby);       // Nobody: stays asleep.
            s.stepOn(head, dist);
            assert(s.until(SystemPhase::WakingHoming, 100) >= 0 && s.fsm.servoActive());
            assert(s.fsm.servoCommand() == s.cfg.bottomDeg);
            const long t = s.until(SystemPhase::ResultHold, 60000);
            assert(t >= 0);
            assert(s.fsm.hasHeight() && std::fabs(s.fsm.heightCm() - head) < 1.0f);
            assert(std::fabs(s.fsm.distanceM() - dist / 100) < 0.02f);
            assert(!s.fsm.servoActive());                         // Detached in RESULT_HOLD.
            std::printf("head %.0f cm at %.1f m -> %.2f cm, stop at %.0f deg (tilt %.1f), %ld ms, %d moves\n",
                        head, dist / 100, s.fsm.heightCm(), s.fsm.servoCommand(), s.fsm.tiltDeg(), t, s.moves);
            assert(s.minMoveGap >= s.cfg.settleMs);              // Step-scan: never faster than settle.
            // 6) Stays while on the scale, then hold_sec after stepping off, then STANDBY.
            s.run(20000);
            assert(s.fsm.phase() == SystemPhase::ResultHold);
            s.stepOff();
            s.run(s.cfg.holdMs() - 500);
            assert(s.fsm.phase() == SystemPhase::ResultHold && s.fsm.hasHeight());
            s.run(1000);
            assert(s.fsm.phase() == SystemPhase::Standby && !s.fsm.hasHeight() && !s.fsm.servoActive());
        }
    }

    // 2) Stepping off in the middle of the sweep -> STANDBY (after the grace time only).
    {
        Sim s;
        s.stepOn(170, 280);
        assert(s.until(SystemPhase::ServoSweep, 5000) >= 0);
        s.run(2000);
        s.stepOff();
        s.run(s.cfg.leaveGraceMs - 200);
        assert(s.fsm.phase() == SystemPhase::ServoSweep);      // Short flicker tolerated.
        assert(s.until(SystemPhase::Standby, 1000) >= 0 && !s.fsm.servoActive());
    }

    // 3) Too close: no angle shows head and feet -> "Step back!", then retry from the bottom.
    {
        Sim s;
        s.stepOn(175, 120);
        assert(s.untilNotice(EdgeNotice::TooClose, 60000) >= 0);
        const float stopped = s.fsm.servoCommand();
        s.run(s.cfg.noticeMs - 200);
        assert(s.fsm.servoCommand() == stopped);               // Waits while the warning shows.
        assert(s.until(SystemPhase::WakingHoming, 1000) >= 0);
        s.distCm = 300;                                         // Person steps back.
        assert(s.until(SystemPhase::ResultHold, 60000) >= 0);
        assert(std::fabs(s.fsm.heightCm() - 175) < 1.0f);
    }

    // 4) Two people: "One at a time", servo does not move until one is left.
    {
        Sim s;
        s.stepOn(170, 280);
        assert(s.until(SystemPhase::ServoSweep, 5000) >= 0);
        s.run(2000);
        s.multi = true;
        assert(s.untilNotice(EdgeNotice::Multiple, 2000) >= 0);
        const float held = s.fsm.servoCommand();
        s.run(5000);
        assert(s.fsm.servoCommand() == held && s.fsm.phase() == SystemPhase::ServoSweep);
        s.multi = false;
        assert(s.until(SystemPhase::ResultHold, 60000) >= 0 && s.fsm.notice() == EdgeNotice::None);
    }

    // 5) Never a full body (person not visible) -> top reached -> "No full body" -> retry.
    {
        Sim s;
        s.stepOn(-1, 280);  // On the scale but outside the camera view.
        assert(s.untilNotice(EdgeNotice::NoFullBody, 90000) >= 0);
        assert(s.fsm.servoCommand() == s.cfg.topDeg);
        assert(s.until(SystemPhase::WakingHoming, s.cfg.noticeMs + 200) >= 0);
    }

    // 7) Camera / laptop silent -> "Camera error"; recovers when frames come back.
    {
        Sim s;
        s.cameraOn = false;
        s.stepOn(170, 280);
        assert(s.untilNotice(EdgeNotice::CameraError, 10000) >= 0);
        s.cameraOn = true;
        assert(s.until(SystemPhase::ResultHold, 60000) >= 0);
    }

    // 8) Scale never locks: after weightTimeoutMs the height is shown anyway.
    {
        Sim s;
        s.stableAfterMs = 1e9f;
        s.stepOn(160, 300);
        assert(s.until(SystemPhase::LockedMeasure, 60000) >= 0);
        assert(s.until(SystemPhase::ResultHold, s.cfg.weightTimeoutMs + 2000) >= 0 && s.fsm.hasHeight());
    }

    // 9) New person steps on during RESULT_HOLD -> new measurement.
    {
        Sim s;
        s.stepOn(170, 280);
        assert(s.until(SystemPhase::ResultHold, 60000) >= 0);
        s.stepOff();
        s.run(s.cfg.leaveGraceMs + 1000);
        assert(s.fsm.phase() == SystemPhase::ResultHold);
        s.stepOn(150, 280);
        assert(s.until(SystemPhase::WakingHoming, 200) >= 0 && !s.fsm.hasHeight());
        assert(s.until(SystemPhase::ResultHold, 60000) >= 0 && std::fabs(s.fsm.heightCm() - 150) < 1.0f);
    }

    // 10) Reversed mounting: larger angle tilts DOWN (servo_dir = +1, TV3 convention),
    //     so the sweep runs 135 -> 45 (hard travel limits).
    {
        EdgeConfig c;
        c.bottomDeg = 135; c.topDeg = 45; c.dir = 1;
        Sim s(c);
        s.stepOn(170, 300);
        assert(s.until(SystemPhase::ResultHold, 60000) >= 0 && std::fabs(s.fsm.heightCm() - 170) < 1.0f);
        assert(s.fsm.tiltDeg() > 0);
    }

    // 11) Narrow sweep + longer settle (slow camera link) still works.
    {
        EdgeConfig c;
        c.bottomDeg = 60; c.topDeg = 120; c.stepDeg = 3; c.settleMs = 900;
        Sim s(c);
        s.stepOn(165, 300);
        assert(s.until(SystemPhase::ResultHold, 60000) >= 0 && std::fabs(s.fsm.heightCm() - 165) < 1.0f);
        assert(s.minMoveGap >= 900);
    }

    // 12) millis() wraps during the measurement.
    {
        Sim s(EdgeConfig(), 0xFFFFF000u);
        s.stepOn(180, 300);
        assert(s.until(SystemPhase::ResultHold, 60000) >= 0 && std::fabs(s.fsm.heightCm() - 180) < 1.0f);
        assert(s.now < 0x10000000u);
    }

    // 13) reset() from anywhere -> STANDBY.
    {
        Sim s;
        s.stepOn(170, 280);
        assert(s.until(SystemPhase::ServoSweep, 5000) >= 0);
        s.fsm.reset(s.now);
        assert(s.fsm.phase() == SystemPhase::Standby && !s.fsm.servoActive());
    }

    std::puts("edge_sweep_fsm_test: OK");
    return 0;
}
