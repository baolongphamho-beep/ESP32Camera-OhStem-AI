#include <cassert>
#include <cmath>
#include "step_scan_fsm.h"

namespace {

StepScanConfig calibratedCfg() {
    StepScanConfig cfg;
    cfg.minDeg = 45.0f;
    cfg.maxDeg = 135.0f;
    cfg.bottomDeg = 112.0f;
    cfg.topDeg = 85.0f;
    cfg.stepDeg = 4.5f;
    cfg.lockedTargetSamples = 3;
    cfg.maxUnlockConsecutiveMisses = 2;
    cfg.maxReversals = 1;
    cfg.weightLockTimeoutMs = 15000;
    return cfg;
}

void testDescendingSweepAndLockFreeze() {
    const auto cfg = calibratedCfg();
    StepScanFsm fsm;
    assert(fsm.phase() == ScanPhase::Idle);

    const auto home = fsm.beginSession(cfg);
    assert(fsm.phase() == ScanPhase::Homing);
    assert(home.action == StepScanAction::MoveToAngle);
    assert(std::fabs(home.targetDeg - 112.0f) < 0.001f);
    assert(home.flushFrames == 1);

    fsm.onHomed(112.0f);
    assert(fsm.phase() == ScanPhase::Sweeping);

    // None -> step toward topDeg (112 -> 107.5).
    auto d1 = fsm.onFrame(cfg, 100, false, FramingStatus::None, false, false);
    assert(d1.action == StepScanAction::MoveToAngle);
    assert(std::fabs(d1.targetDeg - 107.5f) < 0.001f);
    assert(d1.flushFrames == 1);
    fsm.onMoved(d1.targetDeg);

    // TopClipped -> step toward topDeg (107.5 -> 103.0).
    auto d2 = fsm.onFrame(cfg, 200, false, FramingStatus::TopClipped, false, false);
    assert(d2.action == StepScanAction::MoveToAngle);
    assert(std::fabs(d2.targetDeg - 103.0f) < 0.001f);
    assert(d2.flushFrames == 1);
    fsm.onMoved(d2.targetDeg);

    // FullBody sample 1 -> transition to LockedMeasure at 103 deg.
    auto d3 = fsm.onFrame(cfg, 300, false, FramingStatus::FullBody, true, false);
    assert(fsm.phase() == ScanPhase::LockedMeasure);
    assert(std::fabs(fsm.lockedDeg() - 103.0f) < 0.001f);
    assert(fsm.lockedSamples() == 1);
    assert(d3.action == StepScanAction::None);
    assert(d3.useForHeight);
    assert(!d3.heightFrozen);

    // Transient None frame before 3 samples must not unlock or freeze.
    auto d3Miss = fsm.onFrame(cfg, 400, false, FramingStatus::None, false, false);
    assert(fsm.phase() == ScanPhase::LockedMeasure);
    assert(d3Miss.action == StepScanAction::None);
    assert(!d3Miss.useForHeight);

    // Sample 2 + Sample 3 -> height freezes and detaches servo!
    auto d4 = fsm.onFrame(cfg, 500, false, FramingStatus::FullBody, true, false);
    assert(d4.useForHeight);
    assert(!d4.heightFrozen);
    assert(d4.action == StepScanAction::None);
    assert(fsm.lockedSamples() == 2);

    auto d5 = fsm.onFrame(cfg, 600, false, FramingStatus::FullBody, true, false);
    assert(d5.useForHeight);
    assert(d5.heightFrozen);
    assert(fsm.heightFrozen());
    assert(d5.action == StepScanAction::DetachServo);
    assert(!d5.weightTimedOut);

    // After freeze, does not re-emit DetachServo, even if weight hasn't locked yet.
    auto d6 = fsm.onFrame(cfg, 1000, false, FramingStatus::None, false, false);
    assert(d6.heightFrozen);
    assert(d6.action == StepScanAction::None);
    assert(!d6.weightTimedOut);
}

void testAscendingSweepForInvertedServoDir() {
    auto cfg = calibratedCfg();
    cfg.minDeg = 40.0f;
    cfg.maxDeg = 140.0f;
    cfg.bottomDeg = 68.0f;
    cfg.topDeg = 95.0f;
    StepScanFsm fsm;

    const auto home = fsm.beginSession(cfg);
    assert(std::fabs(home.targetDeg - 68.0f) < 0.001f);
    fsm.onHomed(68.0f);

    auto d1 = fsm.onFrame(cfg, 100, false, FramingStatus::TopClipped, false, false);
    assert(d1.action == StepScanAction::MoveToAngle);
    assert(std::fabs(d1.targetDeg - 72.5f) < 0.001f);
    fsm.onMoved(d1.targetDeg);

    auto d2 = fsm.onFrame(cfg, 200, false, FramingStatus::FullBody, true, true);
    assert(fsm.phase() == ScanPhase::LockedMeasure);
    assert(d2.useForHeight);
}

void testTooClosePauseAndResume() {
    const auto cfg = calibratedCfg();
    StepScanFsm fsm;
    fsm.beginSession(cfg);
    fsm.onHomed(112.0f);

    auto dClose = fsm.onFrame(cfg, 100, false, FramingStatus::TooClose, false, false);
    assert(dClose.tooClose);
    assert(dClose.action == StepScanAction::None);
    assert(std::fabs(fsm.currentDeg() - 112.0f) < 0.001f);

    // Person steps back -> resumes normal sweep.
    auto dBack = fsm.onFrame(cfg, 200, false, FramingStatus::TopClipped, false, false);
    assert(!dBack.tooClose);
    assert(dBack.action == StepScanAction::MoveToAngle);
    assert(std::fabs(dBack.targetDeg - 107.5f) < 0.001f);
}

void testMidpointRefinementWithoutOscillation() {
    const auto cfg = calibratedCfg();
    StepScanFsm fsm;
    fsm.beginSession(cfg);
    fsm.onHomed(112.0f);

    // Step forward from 112 -> 107.5 after TopClipped.
    auto d1 = fsm.onFrame(cfg, 100, false, FramingStatus::TopClipped, false, false);
    assert(d1.action == StepScanAction::MoveToAngle);
    assert(std::fabs(d1.targetDeg - 107.5f) < 0.001f);
    fsm.onMoved(d1.targetDeg);

    // Now feet clipped -> reverse 1 half-step to midpoint (112 + 107.5) / 2 = 109.75.
    auto dRev = fsm.onFrame(cfg, 200, false, FramingStatus::BottomClipped, false, false);
    assert(dRev.action == StepScanAction::MoveToAngle);
    assert(std::fabs(dRev.targetDeg - 109.75f) < 0.001f);
    fsm.onMoved(dRev.targetDeg);

    // If midpoint still hits TopClipped after reversing, must declare TooClose (no oscillation!).
    auto dStopTop = fsm.onFrame(cfg, 300, false, FramingStatus::TopClipped, false, false);
    assert(dStopTop.action == StepScanAction::None);
    assert(dStopTop.tooClose);

    // Test exceeding maxReversals on repeated BottomClipped as well.
    StepScanFsm fsm2;
    fsm2.beginSession(cfg);
    fsm2.onHomed(112.0f);
    auto s1 = fsm2.onFrame(cfg, 100, false, FramingStatus::TopClipped, false, false);
    fsm2.onMoved(s1.targetDeg);
    auto sRev = fsm2.onFrame(cfg, 200, false, FramingStatus::BottomClipped, false, false);
    assert(sRev.action == StepScanAction::MoveToAngle);
    fsm2.onMoved(sRev.targetDeg);
    auto sStopBot = fsm2.onFrame(cfg, 300, false, FramingStatus::BottomClipped, false, false);
    assert(sStopBot.action == StepScanAction::None);
    assert(sStopBot.tooClose);
}

void testSideClippedMultipleAndExhausted() {
    auto cfg = calibratedCfg();
    cfg.bottomDeg = 90.0f;
    cfg.topDeg = 85.0f;
    cfg.stepDeg = 5.0f;
    StepScanFsm fsm;
    fsm.beginSession(cfg);
    fsm.onHomed(90.0f);

    auto dMulti = fsm.onFrame(cfg, 100, true, FramingStatus::FullBody, false, false);
    assert(dMulti.multiplePeople);
    assert(dMulti.action == StepScanAction::None);
    assert(fsm.phase() == ScanPhase::Sweeping);

    auto dSide = fsm.onFrame(cfg, 200, false, FramingStatus::SideClipped, false, false);
    assert(dSide.sideClipped);
    assert(dSide.action == StepScanAction::None);

    auto dStep = fsm.onFrame(cfg, 300, false, FramingStatus::TopClipped, false, false);
    assert(dStep.action == StepScanAction::MoveToAngle);
    assert(std::fabs(dStep.targetDeg - 85.0f) < 0.001f);
    fsm.onMoved(dStep.targetDeg);

    auto dExh = fsm.onFrame(cfg, 400, false, FramingStatus::TopClipped, false, false);
    assert(dExh.sweepExhausted);
    assert(dExh.tooClose);
    assert(dExh.action == StepScanAction::None);
}

void testUnlockResweepAndWeightTimeout() {
    const auto cfg = calibratedCfg();
    StepScanFsm fsm;
    fsm.beginSession(cfg);
    fsm.onHomed(103.0f);

    auto d1 = fsm.onFrame(cfg, 1000, false, FramingStatus::FullBody, true, false);
    assert(fsm.phase() == ScanPhase::LockedMeasure);
    assert(fsm.lockedSamples() == 1);
    assert(!d1.heightFrozen);

    // 1st TopClipped stays locked (tolerance = 2 consecutive misses).
    auto m1 = fsm.onFrame(cfg, 1500, false, FramingStatus::TopClipped, false, false);
    assert(fsm.phase() == ScanPhase::LockedMeasure);
    assert(m1.action == StepScanAction::None);

    // 2nd consecutive TopClipped unlocks and immediately steps toward topDeg!
    auto m2 = fsm.onFrame(cfg, 2000, false, FramingStatus::TopClipped, false, false);
    assert(fsm.phase() == ScanPhase::Sweeping);
    assert(m2.action == StepScanAction::MoveToAngle);
    assert(std::fabs(m2.targetDeg - 98.5f) < 0.001f);
    fsm.onMoved(m2.targetDeg);

    // Re-lock and test 15 s weight timeout (including uint32 rollover).
    const uint32_t lockTime = 0xffffff00U;
    fsm.onFrame(cfg, lockTime, false, FramingStatus::FullBody, true, false);
    fsm.onFrame(cfg, lockTime + 100U, false, FramingStatus::FullBody, true, false);
    fsm.onFrame(cfg, lockTime + 200U, false, FramingStatus::FullBody, true, false);
    assert(fsm.heightFrozen());

    auto beforeTimeout = fsm.onFrame(cfg, lockTime + 14999U, false, FramingStatus::None, false, false);
    assert(!beforeTimeout.weightTimedOut);

    auto atTimeout = fsm.onFrame(cfg, lockTime + 15000U, false, FramingStatus::None, false, false);
    assert(atTimeout.weightTimedOut);

    auto withStableWeight = fsm.onFrame(cfg, lockTime + 16000U, false, FramingStatus::None, false, true);
    assert(!withStableWeight.weightTimedOut);
}

void testServoCaptureSyncInvariants() {
    ServoCaptureSync sync;
    assert(sync.canCapture());
    assert(sync.canSleep());

    // Starting servo session blocks capture until homing completes.
    sync.onSessionStart(true);
    assert(!sync.canCapture());
    assert(sync.canSleep());

    sync.beginHardwareMove();
    assert(!sync.canCapture());
    assert(!sync.canSleep());

    // Pre-move or mid-move ticket must be dropped as stale.
    const auto staleTicket = sync.startCapture();
    sync.endHardwareMove(107.5f, 1);
    assert(sync.canSleep());
    assert(sync.canCapture());
    assert(sync.finishCaptureSuccess(staleTicket) == ServoCaptureSync::CaptureDisposition::DropStale);

    // First capture after settle consumes the 1-frame flush budget.
    const auto flushTicket = sync.startCapture();
    assert(flushTicket.isFlush);
    assert(std::fabs(flushTicket.servoDeg - 107.5f) < 0.001f);
    assert(sync.finishCaptureSuccess(flushTicket) == ServoCaptureSync::CaptureDisposition::ConsumedFlush);

    // Next capture at the same pose publishes normally.
    const auto liveTicket = sync.startCapture();
    assert(!liveTicket.isFlush);
    assert(sync.finishCaptureSuccess(liveTicket) == ServoCaptureSync::CaptureDisposition::PublishFrame);

    // If a move begins while an HTTP ticket is in flight, its completion is DropStale.
    const auto inFlight = sync.startCapture();
    sync.beginHardwareMove();
    assert(sync.finishCaptureSuccess(inFlight) == ServoCaptureSync::CaptureDisposition::DropStale);
    sync.abortHardwareMove();
    assert(sync.canSleep());

    // Error on a flush capture clears remaining flush count cleanly.
    sync.beginHardwareMove();
    sync.endHardwareMove(103.0f, 1);
    const auto errFlush = sync.startCapture();
    sync.finishCaptureError(errFlush);
    const auto afterErr = sync.startCapture();
    assert(!afterErr.isFlush);

    // Session start(false) / clearHomingPending immediately allows capture.
    sync.onSessionStart(true);
    assert(!sync.canCapture());
    sync.clearHomingPending(90.0f);
    assert(sync.canCapture());
    sync.onSessionStop();
    assert(sync.canSleep());
}

} // namespace

int main() {
    testDescendingSweepAndLockFreeze();
    testAscendingSweepForInvertedServoDir();
    testTooClosePauseAndResume();
    testMidpointRefinementWithoutOscillation();
    testSideClippedMultipleAndExhausted();
    testUnlockResweepAndWeightTimeout();
    testServoCaptureSyncInvariants();
    return 0;
}
