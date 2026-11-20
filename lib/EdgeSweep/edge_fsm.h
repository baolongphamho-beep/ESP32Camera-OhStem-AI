#pragma once
// Plan 2 state machine (5 phases). Pure C++, tested by tests/edge_sweep_fsm_test.cpp.
//
//   STANDBY        servo detached, LCD dark          -> WAKING_HOMING when weight >= wakeKg
//   WAKING_HOMING  servo to servo_bottom             -> SERVO_SWEEP after homing + settle
//   SERVO_SWEEP    step-scan bottom -> top: move one step, wait servo_settle, take the
//                  next frame, analyzeFraming()       -> LOCKED_MEASURE on FullBody
//                  TooClose -> "Step back!", end of travel -> "No full body", then restart
//   LOCKED_MEASURE servo holds; median height of lockFrames full-body frames,
//                  then wait for the scale lock      -> RESULT_HOLD
//   RESULT_HOLD    servo detached, result on LCD while on the scale and hold_sec after
//                  stepping off                      -> STANDBY (or a new person -> WAKING)
// Leaving the scale (weight gone > leaveGraceMs) before a height returns to STANDBY.
// All times are millis() and wrap safely.
#include <cmath>
#include <cstdint>

#include "edge_config.h"
#include "edge_geometry.h"
#include "edge_message.h"
#include "servo_ctrl.h"  // servoToTiltDeg() only (pure); the FSM never drives the pin itself.

enum class SystemPhase { Standby, WakingHoming, ServoSweep, LockedMeasure, ResultHold };
enum class EdgeNotice { None, TooClose, Multiple, NoFullBody, CameraError };

inline const char* phaseName(SystemPhase p) {
    switch (p) {
        case SystemPhase::Standby: return "STANDBY";
        case SystemPhase::WakingHoming: return "WAKING_HOMING";
        case SystemPhase::ServoSweep: return "SERVO_SWEEP";
        case SystemPhase::LockedMeasure: return "LOCKED_MEASURE";
        case SystemPhase::ResultHold: return "RESULT_HOLD";
    }
    return "?";
}

inline const char* noticeName(EdgeNotice n) {
    switch (n) {
        case EdgeNotice::None: return "";
        case EdgeNotice::TooClose: return "TooClose";
        case EdgeNotice::Multiple: return "Multiple";
        case EdgeNotice::NoFullBody: return "NoFullBody";
        case EdgeNotice::CameraError: return "CameraError";
    }
    return "";
}

class EdgeFsm {
public:
    explicit EdgeFsm(const EdgeConfig& cfg = EdgeConfig()) : cfg_(cfg) {}

    void setConfig(const EdgeConfig& cfg) {
        cfg_ = cfg;
        command_ = clamp(command_);
    }
    const EdgeConfig& config() const { return cfg_; }

    void begin(uint32_t now) {
        present_ = stable_ = false;
        lastPresentAt_ = now - cfg_.leaveGraceMs;
        command_ = clamp(cfg_.bottomDeg);
        enter(now, SystemPhase::Standby);
    }

    void reset(uint32_t now) { enter(now, SystemPhase::Standby); }

    // Live scale data: hasWeight = someone on the scale now (MeasurementView::hasWeight).
    void setScale(uint32_t now, bool hasWeight, float kg, bool stable) {
        present_ = hasWeight && std::isfinite(kg) && kg >= cfg_.wakeKg;
        stable_ = present_ && stable;
        if (present_) lastPresentAt_ = now;
    }

    void onFrame(uint32_t now, const BodyMessage& m) {
        if (m.type == BodyMessageType::Invalid) return;
        lastFrameAt_ = now;
        waitFrom_ = now;
        if (notice_ == EdgeNotice::CameraError) notice_ = EdgeNotice::None;
        if (phase_ != SystemPhase::ServoSweep && phase_ != SystemPhase::LockedMeasure) return;
        if (now - lastMoveAt_ < cfg_.settleMs) return;  // Frame may predate the last move.
        if (phase_ == SystemPhase::ServoSweep &&
            (notice_ == EdgeNotice::TooClose || notice_ == EdgeNotice::NoFullBody)) return;

        if (m.type == BodyMessageType::Multi) {
            notice_ = EdgeNotice::Multiple;
            framing_ = FramingStatus::None;
            return;  // Wait, do not move, until one person is left.
        }
        if (notice_ == EdgeNotice::Multiple) notice_ = EdgeNotice::None;
        framing_ = FramingStatus::None;
        if (m.type == BodyMessageType::Box) {
            PersonBox box{m.x1 * kScale, m.y1 * kScale, m.x2 * kScale, m.y2 * kScale,
                          (float)m.confidence / 100.0f};
            framing_ = analyzeFraming(box, (int)kScale, (int)kScale, cfg_.marginFrac * kScale);
        }

        if (phase_ == SystemPhase::LockedMeasure) {
            if (!hasHeight_ && framing_ == FramingStatus::FullBody) addSample(now, m);
            return;  // Other frames while locked: person adjusting, ignore.
        }
        // SERVO_SWEEP
        if (framing_ == FramingStatus::FullBody) {
            enter(now, SystemPhase::LockedMeasure);
            addSample(now, m);
        } else if (framing_ == FramingStatus::TooClose) {
            setNotice(now, EdgeNotice::TooClose);
        } else {
            step(now);  // None / TopClipped / BottomClipped / SideClipped: keep scanning.
        }
    }

    void update(uint32_t now) {
        const bool absent = !present_ && now - lastPresentAt_ >= cfg_.leaveGraceMs;
        switch (phase_) {
            case SystemPhase::Standby:
                if (present_) enter(now, SystemPhase::WakingHoming);
                break;
            case SystemPhase::WakingHoming:
                if (absent) enter(now, SystemPhase::Standby);
                else if (now - stateAt_ >= cfg_.homingMs + cfg_.settleMs) enter(now, SystemPhase::ServoSweep);
                break;
            case SystemPhase::ServoSweep:
                if (absent) { enter(now, SystemPhase::Standby); break; }
                if ((notice_ == EdgeNotice::TooClose || notice_ == EdgeNotice::NoFullBody) &&
                    now - noticeAt_ >= cfg_.noticeMs) {
                    enter(now, SystemPhase::WakingHoming);  // Try again from the bottom.
                    break;
                }
                checkCamera(now);
                break;
            case SystemPhase::LockedMeasure:
                if (hasHeight_) {
                    if (stable_ || absent || now - heightAt_ >= cfg_.weightTimeoutMs)
                        enter(now, SystemPhase::ResultHold);
                } else if (absent) {
                    enter(now, SystemPhase::Standby);
                } else if (now - stateAt_ >= cfg_.lockTimeoutMs) {
                    phase_ = SystemPhase::ServoSweep;  // Lost the full body: continue upwards.
                    stateAt_ = now;
                    step(now);
                } else {
                    checkCamera(now);
                }
                break;
            case SystemPhase::ResultHold:
                if (present_ && left_) {
                    enter(now, SystemPhase::WakingHoming);  // A new person stepped on.
                } else if (!present_) {
                    left_ = left_ || absent;
                    if (now - lastPresentAt_ >= cfg_.holdMs()) enter(now, SystemPhase::Standby);
                }
                break;
        }
    }

    SystemPhase phase() const { return phase_; }
    EdgeNotice notice() const { return notice_; }
    FramingStatus framing() const { return framing_; }
    float servoCommand() const { return command_; }
    bool servoActive() const { return servoActive_; }
    float tiltDeg() const { return servoToTiltDeg(command_, cfg_); }
    bool hasHeight() const { return hasHeight_; }
    float heightCm() const { return height_; }
    float distanceM() const { return distance_; }
    int samples() const { return samples_; }
    bool present() const { return present_; }  // Someone (>= wakeKg) on the scale right now.

private:
    static constexpr float kScale = 1000.0f;  // Normalized box -> "pixels" for analyzeFraming.

    float lowDeg() const { return cfg_.bottomDeg < cfg_.topDeg ? cfg_.bottomDeg : cfg_.topDeg; }
    float highDeg() const { return cfg_.bottomDeg < cfg_.topDeg ? cfg_.topDeg : cfg_.bottomDeg; }
    float clamp(float c) const { return c < lowDeg() ? lowDeg() : (c > highDeg() ? highDeg() : c); }

    void moveTo(uint32_t now, float c) {
        command_ = clamp(c);
        lastMoveAt_ = now;
        waitFrom_ = now;
    }

    void setNotice(uint32_t now, EdgeNotice n) {
        notice_ = n;
        noticeAt_ = now;
    }

    void step(uint32_t now) {
        const float dir = cfg_.topDeg >= cfg_.bottomDeg ? 1.0f : -1.0f;
        if (std::fabs(command_ - clamp(cfg_.topDeg)) < 1e-3f) {
            setNotice(now, EdgeNotice::NoFullBody);  // Whole travel done, never full body.
            return;
        }
        moveTo(now, command_ + dir * cfg_.stepDeg);
    }

    void checkCamera(uint32_t now) {
        if (notice_ == EdgeNotice::None && now - waitFrom_ > cfg_.settleMs + cfg_.frameTimeoutMs)
            notice_ = EdgeNotice::CameraError;
    }

    void addSample(uint32_t now, const BodyMessage& m) {
        const edge_geometry::HeightResult r = edge_geometry::heightFromBox(
            m.y1, m.y2, tiltDeg(), cfg_.cameraHeightCm, cfg_.focal);
        if (!r.valid || samples_ >= 8) return;
        heights_[samples_] = r.heightCm;
        distances_[samples_] = r.distanceM;
        ++samples_;
        const int need = cfg_.lockFrames < 1 ? 1 : (cfg_.lockFrames > 8 ? 8 : cfg_.lockFrames);
        if (samples_ >= need) {
            height_ = edge_geometry::median(heights_, samples_);
            distance_ = edge_geometry::median(distances_, samples_);
            hasHeight_ = true;
            heightAt_ = now;
        }
    }

    void enter(uint32_t now, SystemPhase p) {
        phase_ = p;
        stateAt_ = now;
        switch (p) {
            case SystemPhase::Standby:
                servoActive_ = false;
                hasHeight_ = false;
                samples_ = 0;
                notice_ = EdgeNotice::None;
                framing_ = FramingStatus::None;
                break;
            case SystemPhase::WakingHoming:
                servoActive_ = true;
                hasHeight_ = false;
                samples_ = 0;
                left_ = false;
                notice_ = EdgeNotice::None;
                framing_ = FramingStatus::None;
                moveTo(now, cfg_.bottomDeg);
                break;
            case SystemPhase::ServoSweep:
                waitFrom_ = now;
                lastMoveAt_ = now - cfg_.settleMs;  // Homing already waited for the settle.
                break;
            case SystemPhase::LockedMeasure:
                samples_ = 0;
                hasHeight_ = false;
                break;
            case SystemPhase::ResultHold:
                servoActive_ = false;  // Detach: saves power, no buzzing.
                left_ = !present_;
                break;
        }
    }

    EdgeConfig cfg_;
    SystemPhase phase_ = SystemPhase::Standby;
    EdgeNotice notice_ = EdgeNotice::None;
    FramingStatus framing_ = FramingStatus::None;
    float command_ = 0;
    bool servoActive_ = false, present_ = false, stable_ = false, hasHeight_ = false, left_ = false;
    uint32_t stateAt_ = 0, lastMoveAt_ = 0, waitFrom_ = 0, lastFrameAt_ = 0, noticeAt_ = 0;
    uint32_t lastPresentAt_ = 0, heightAt_ = 0;
    int samples_ = 0;
    float heights_[8] = {}, distances_[8] = {};
    float height_ = 0, distance_ = 0;
};
