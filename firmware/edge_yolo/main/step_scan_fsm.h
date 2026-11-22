#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "edge_geometry.h"
#include "servo_math.h"

enum class ScanPhase : uint8_t {
    Idle,
    Homing,
    Sweeping,
    LockedMeasure,
};

inline const char* scanPhaseName(ScanPhase phase) {
    switch (phase) {
        case ScanPhase::Idle: return "STANDBY";
        case ScanPhase::Homing: return "WAKING_HOMING";
        case ScanPhase::Sweeping: return "SERVO_SWEEP";
        case ScanPhase::LockedMeasure: return "LOCKED_MEASURE";
    }
    return "STANDBY";
}

struct StepScanConfig {
    float minDeg = kServoDefaultMinDeg;
    float maxDeg = kServoDefaultMaxDeg;
    float bottomDeg = kServoDefaultBottomDeg;
    float topDeg = kServoDefaultTopDeg;
    float stepDeg = kServoDefaultStepDeg;
    uint8_t lockedTargetSamples = 3;
    uint8_t maxUnlockConsecutiveMisses = 2;
    uint8_t maxReversals = 1;
    uint32_t weightLockTimeoutMs = 15000;
};

enum class StepScanAction : uint8_t {
    None,
    MoveToAngle,
    DetachServo,
};

struct StepScanDecision {
    StepScanAction action = StepScanAction::None;
    float targetDeg = kServoDefaultLevelDeg;
    uint8_t flushFrames = 0;
    bool tooClose = false;
    bool sideClipped = false;
    bool sweepExhausted = false;
    bool multiplePeople = false;
    bool useForHeight = false;
    bool heightFrozen = false;
    bool weightTimedOut = false;
};

class StepScanFsm {
public:
    void reset() {
        phase_ = ScanPhase::Idle;
        currentDeg_ = kServoDefaultLevelDeg;
        prevDeg_ = kServoDefaultLevelDeg;
        lockedDeg_ = kServoDefaultLevelDeg;
        lastDir_ = 0;
        reversals_ = 0;
        lockedSamples_ = 0;
        unlockMisses_ = 0;
        lockedAtMs_ = 0;
        sweepExhausted_ = false;
        heightFrozen_ = false;
        detachedAfterLock_ = false;
    }

    ScanPhase phase() const { return phase_; }
    float currentDeg() const { return currentDeg_; }
    float lockedDeg() const { return lockedDeg_; }
    uint8_t lockedSamples() const { return lockedSamples_; }
    bool heightFrozen() const { return heightFrozen_; }

    StepScanDecision beginSession(const StepScanConfig& cfg) {
        reset();
        phase_ = ScanPhase::Homing;
        StepScanDecision dec;
        dec.action = StepScanAction::MoveToAngle;
        dec.targetDeg = clampServoDeg(cfg.bottomDeg, cfg.minDeg, cfg.maxDeg);
        dec.flushFrames = 1;
        return dec;
    }

    void onHomed(float settledDeg) {
        phase_ = ScanPhase::Sweeping;
        currentDeg_ = settledDeg;
        prevDeg_ = settledDeg;
        lastDir_ = 0;
        reversals_ = 0;
        lockedSamples_ = 0;
        unlockMisses_ = 0;
        sweepExhausted_ = false;
        heightFrozen_ = false;
        detachedAfterLock_ = false;
    }

    void onMoved(float settledDeg) {
        currentDeg_ = settledDeg;
    }

    StepScanDecision onFrame(const StepScanConfig& cfg,
                             uint32_t nowMs,
                             bool hasMultiple,
                             FramingStatus framing,
                             bool heightValid,
                             bool weightStable) {
        StepScanDecision dec;
        if (phase_ == ScanPhase::Idle || phase_ == ScanPhase::Homing) {
            return dec;
        }
        if (phase_ == ScanPhase::Sweeping) {
            return handleSweepingFrame(cfg, nowMs, hasMultiple, framing, heightValid);
        }

        // Phase == LockedMeasure
        const bool timedOut = !weightStable &&
            (static_cast<uint32_t>(nowMs - lockedAtMs_) >= cfg.weightLockTimeoutMs);

        if (heightFrozen_) {
            dec.heightFrozen = true;
            dec.weightTimedOut = timedOut;
            if (!detachedAfterLock_) {
                detachedAfterLock_ = true;
                dec.action = StepScanAction::DetachServo;
            }
            return dec;
        }
        if (hasMultiple) {
            dec.multiplePeople = true;
            unlockMisses_ = 0;
            dec.weightTimedOut = timedOut;
            return dec;
        }
        if (framing == FramingStatus::FullBody) {
            unlockMisses_ = 0;
            dec.useForHeight = heightValid;
            dec.weightTimedOut = timedOut;
            if (heightValid) {
                ++lockedSamples_;
                if (lockedSamples_ >= std::max<uint8_t>(1, cfg.lockedTargetSamples)) {
                    heightFrozen_ = true;
                    dec.heightFrozen = true;
                    detachedAfterLock_ = true;
                    dec.action = StepScanAction::DetachServo;
                }
            }
            return dec;
        }
        if (framing == FramingStatus::TopClipped || framing == FramingStatus::BottomClipped) {
            if (++unlockMisses_ >= std::max<uint8_t>(1, cfg.maxUnlockConsecutiveMisses)) {
                phase_ = ScanPhase::Sweeping;
                lockedSamples_ = 0;
                unlockMisses_ = 0;
                reversals_ = 0;
                lastDir_ = 0;
                sweepExhausted_ = false;
                heightFrozen_ = false;
                detachedAfterLock_ = false;
                return handleSweepingFrame(cfg, nowMs, false, framing, heightValid);
            }
            dec.weightTimedOut = timedOut;
            return dec;
        }
        if (framing == FramingStatus::TooClose) {
            dec.tooClose = true;
            dec.weightTimedOut = timedOut;
            return dec;
        }
        if (framing == FramingStatus::SideClipped) {
            dec.sideClipped = true;
            dec.weightTimedOut = timedOut;
            return dec;
        }
        dec.weightTimedOut = timedOut;
        return dec;
    }

private:
    StepScanDecision handleSweepingFrame(const StepScanConfig& cfg,
                                         uint32_t nowMs,
                                         bool hasMultiple,
                                         FramingStatus framing,
                                         bool heightValid) {
        StepScanDecision dec;
        const float bottomClamped = clampServoDeg(cfg.bottomDeg, cfg.minDeg, cfg.maxDeg);
        const float topClamped = clampServoDeg(cfg.topDeg, cfg.minDeg, cfg.maxDeg);

        if (hasMultiple) {
            dec.multiplePeople = true;
            return dec;
        }
        if (framing == FramingStatus::FullBody) {
            phase_ = ScanPhase::LockedMeasure;
            lockedDeg_ = currentDeg_;
            lockedAtMs_ = nowMs;
            unlockMisses_ = 0;
            lockedSamples_ = heightValid ? 1 : 0;
            heightFrozen_ = false;
            detachedAfterLock_ = false;
            dec.useForHeight = heightValid;
            if (heightValid && lockedSamples_ >= std::max<uint8_t>(1, cfg.lockedTargetSamples)) {
                heightFrozen_ = true;
                dec.heightFrozen = true;
                detachedAfterLock_ = true;
                dec.action = StepScanAction::DetachServo;
            }
            return dec;
        }
        if (framing == FramingStatus::TooClose) {
            dec.tooClose = true;
            return dec;
        }
        if (framing == FramingStatus::SideClipped) {
            dec.sideClipped = true;
            return dec;
        }
        if (framing == FramingStatus::None || framing == FramingStatus::TopClipped) {
            if (framing == FramingStatus::TopClipped && lastDir_ < 0) {
                dec.tooClose = true;
                return dec;
            }
            float nextDeg = currentDeg_;
            if (servoNextSweepDeg(currentDeg_, bottomClamped, topClamped, cfg.stepDeg, nextDeg)) {
                prevDeg_ = currentDeg_;
                lastDir_ = 1;
                dec.action = StepScanAction::MoveToAngle;
                dec.targetDeg = clampServoDeg(nextDeg, cfg.minDeg, cfg.maxDeg);
                dec.flushFrames = 1;
                return dec;
            }
            sweepExhausted_ = true;
            dec.sweepExhausted = true;
            if (framing == FramingStatus::TopClipped) {
                dec.tooClose = true;
            }
            return dec;
        }
        if (framing == FramingStatus::BottomClipped) {
            if (lastDir_ > 0 && reversals_ < cfg.maxReversals) {
                const float mid = clampServoDeg(0.5f * (prevDeg_ + currentDeg_), cfg.minDeg, cfg.maxDeg);
                if (std::fabs(mid - currentDeg_) >= 0.5f) {
                    ++reversals_;
                    lastDir_ = -1;
                    dec.action = StepScanAction::MoveToAngle;
                    dec.targetDeg = mid;
                    dec.flushFrames = 1;
                    return dec;
                }
                dec.tooClose = true;
                return dec;
            }
            if (reversals_ >= cfg.maxReversals) {
                dec.tooClose = true;
                return dec;
            }
            float nextDeg = currentDeg_;
            if (servoNextSweepDeg(currentDeg_, topClamped, bottomClamped, cfg.stepDeg, nextDeg)) {
                prevDeg_ = currentDeg_;
                lastDir_ = -1;
                dec.action = StepScanAction::MoveToAngle;
                dec.targetDeg = clampServoDeg(nextDeg, cfg.minDeg, cfg.maxDeg);
                dec.flushFrames = 1;
                return dec;
            }
            sweepExhausted_ = true;
            dec.sweepExhausted = true;
            dec.tooClose = true;
            return dec;
        }
        return dec;
    }

    ScanPhase phase_ = ScanPhase::Idle;
    float currentDeg_ = kServoDefaultLevelDeg;
    float prevDeg_ = kServoDefaultLevelDeg;
    float lockedDeg_ = kServoDefaultLevelDeg;
    int8_t lastDir_ = 0;
    uint8_t reversals_ = 0;
    uint8_t lockedSamples_ = 0;
    uint8_t unlockMisses_ = 0;
    uint32_t lockedAtMs_ = 0;
    bool sweepExhausted_ = false;
    bool heightFrozen_ = false;
    bool detachedAfterLock_ = false;
};

// Synchronizes Core 1 servo motion/homing with Core 0 HTTP capture + buffer flush.
class ServoCaptureSync {
public:
    struct CaptureTicket {
        uint32_t poseEpoch = 0;
        float servoDeg = kServoDefaultLevelDeg;
        bool isFlush = false;
    };

    enum class CaptureDisposition : uint8_t {
        DropStale,
        ConsumedFlush,
        PublishFrame,
    };

    void onSessionStart(bool servoEnabled) {
        homingPending_ = servoEnabled;
        ++poseEpoch_;
        flushRemaining_ = 0;
    }

    void onSessionStop() {
        homingPending_ = false;
        ++poseEpoch_;
        flushRemaining_ = 0;
    }

    void beginHardwareMove() {
        hwBusy_ = true;
        homingPending_ = false;
        ++poseEpoch_;
        flushRemaining_ = 0;
    }

    void endHardwareMove(float settledDeg, uint8_t flushFrames) {
        hwBusy_ = false;
        homingPending_ = false;
        settledDeg_ = settledDeg;
        flushRemaining_ = flushFrames;
        ++poseEpoch_;
    }

    void abortHardwareMove() {
        hwBusy_ = false;
        ++poseEpoch_;
        flushRemaining_ = 0;
    }

    void clearHomingPending(float currentDeg) {
        homingPending_ = false;
        settledDeg_ = currentDeg;
        flushRemaining_ = 0;
        ++poseEpoch_;
    }

    bool canCapture() const { return !hwBusy_ && !homingPending_; }
    bool canSleep() const { return !hwBusy_; }
    uint32_t poseEpoch() const { return poseEpoch_; }

    CaptureTicket startCapture() const {
        return {poseEpoch_, settledDeg_, flushRemaining_ > 0};
    }

    CaptureDisposition finishCaptureSuccess(const CaptureTicket& ticket) {
        if (hwBusy_ || homingPending_ || ticket.poseEpoch != poseEpoch_) {
            return CaptureDisposition::DropStale;
        }
        if (ticket.isFlush) {
            if (flushRemaining_ > 0) {
                --flushRemaining_;
            }
            return CaptureDisposition::ConsumedFlush;
        }
        return CaptureDisposition::PublishFrame;
    }

    void finishCaptureError(const CaptureTicket& ticket) {
        if (!hwBusy_ && !homingPending_ && ticket.poseEpoch == poseEpoch_ &&
            ticket.isFlush && flushRemaining_ > 0) {
            flushRemaining_ = 0;
        }
    }

private:
    bool homingPending_ = false;
    bool hwBusy_ = false;
    uint32_t poseEpoch_ = 0;
    float settledDeg_ = kServoDefaultLevelDeg;
    uint8_t flushRemaining_ = 0;
};
