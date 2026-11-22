#pragma once
// Pure motion / conversion logic for the tilt servo. No ESP-IDF dependency so it
// can be unit-tested with g++ on a PC (tests/servo_motion_test.cpp).
// Origin: TV3 servo_ctrl.zip (22/11/2026). Shared by the native driver
// (firmware/edge_yolo/main/servo_ctrl.cpp), the Arduino driver (lib/EdgeSweep/servo_ctrl.cpp)
// and the Plan 2 FSM (edge_fsm.h, to know when a move has finished).
#include <algorithm>
#include <cmath>
#include <cstdint>

// ---------------------------------------------------------------------------
// Angle <-> pulse <-> LEDC duty
// ---------------------------------------------------------------------------
struct ServoPulse {
    float minUs = 500;    // pulse width at 0 deg   (MG90S / MG996R: ~500 us)
    float maxUs = 2400;   // pulse width at 180 deg (MG90S / MG996R: ~2400-2500 us)
    float periodUs = 20000; // 50 Hz
};

inline float servoPulseUs(float deg, const ServoPulse& p) {
    deg = std::min(180.f, std::max(0.f, deg));
    return p.minUs + (p.maxUs - p.minUs) * deg / 180.f;
}

inline uint32_t servoDuty(float deg, const ServoPulse& p, int bits = 14) {
    const float full = float((1u << bits) - 1);
    const float duty = servoPulseUs(deg, p) / p.periodUs * float(1u << bits);
    return uint32_t(std::lround(std::min(full, std::max(0.f, duty))));
}

// ---------------------------------------------------------------------------
// Servo angle -> real camera pitch (README convention: DOWN positive, UP negative)
//   tilt = dir * (servoDeg - levelDeg)
// levelDeg = servo angle at which the camera looks exactly horizontal.
// dir      = +1 if increasing servo angle tilts the camera DOWN, -1 if UP.
// ---------------------------------------------------------------------------
inline float servoTiltFromDeg(float servoDeg, float levelDeg, int dir) {
    return (dir < 0 ? -1.f : 1.f) * (servoDeg - levelDeg);
}
inline float servoDegFromTilt(float tiltDeg, float levelDeg, int dir) {
    return levelDeg + (dir < 0 ? -1.f : 1.f) * tiltDeg;
}

// ---------------------------------------------------------------------------
// Step-scan helpers. The camera returns images that are ~camDelayMs old, so a frame
// only shows the CURRENT angle if its request started at least settle + camDelay
// after the servo came to rest. Anything earlier shows a previous angle: ignore it.
// stillSinceMs == 0 means "servo is moving".
// ---------------------------------------------------------------------------
inline bool servoFrameFresh(uint32_t startedMs, uint32_t stillSinceMs, uint32_t settleMs, uint32_t camDelayMs) {
    return stillSinceMs != 0 && int32_t(startedMs - (stillSinceMs + settleMs + camDelayMs)) >= 0;
}
// Next angle of the step-scan (cur + step, clamped to max). false = already at the top.
inline bool servoNextStep(float cur, float stepDeg, float maxDeg, float& next) {
    if (cur >= maxDeg - 0.5f) return false;
    next = std::min(maxDeg, cur + std::max(0.5f, stepDeg));
    return true;
}

// ---------------------------------------------------------------------------
// Trapezoidal motion generator. step(dt) is called from a fixed 20 ms tick.
//  - setTarget()/sweep: accelerate to `cruise`, brake with the same accel so it
//    arrives exactly at the target (smooth, no jerk on the camera cable).
//  - stop(): IMMEDIATE hold at the current position (v = 0 in the same tick).
// ---------------------------------------------------------------------------
class ServoMotion {
public:
    void configure(float minDeg, float maxDeg, float accelDps2 = 240.f) {
        minDeg_ = minDeg; maxDeg_ = maxDeg; accel_ = std::max(1.f, accelDps2);
        pos_ = clampDeg(pos_); target_ = clampDeg(target_);
    }
    void reset(float deg) { pos_ = target_ = clampDeg(deg); vel_ = 0; active_ = false; }

    void setTarget(float deg, float cruiseDps) {
        target_ = clampDeg(deg);
        cruise_ = std::max(1.f, cruiseDps);
        active_ = std::fabs(target_ - pos_) > kEps;
    }
    void stop() { target_ = pos_; vel_ = 0; active_ = false; }

    // Returns true when this step finished the move.
    bool step(float dt) {
        if (!active_) return false;
        const float dist = target_ - pos_;
        const float remain = std::fabs(dist);
        const float dir = dist >= 0 ? 1.f : -1.f;
        if (remain <= kEps) { finish(); return true; }
        const float brake = std::sqrt(2.f * accel_ * remain); // fastest speed that can still stop in time
        const float vDes = dir * std::min(cruise_, brake);
        const float dv = accel_ * dt;
        vel_ += std::min(dv, std::max(-dv, vDes - vel_));
        const float move = vel_ * dt;
        if (std::fabs(move) >= remain) { finish(); return true; }
        pos_ = clampDeg(pos_ + move);
        return false;
    }

    float pos() const { return pos_; }
    float vel() const { return vel_; }
    float target() const { return target_; }
    bool moving() const { return active_; }
    float minDeg() const { return minDeg_; }
    float maxDeg() const { return maxDeg_; }
    float clampDeg(float d) const { return std::min(maxDeg_, std::max(minDeg_, d)); }

private:
    static constexpr float kEps = 0.02f;
    void finish() { pos_ = target_; vel_ = 0; active_ = false; }
    float minDeg_ = 0, maxDeg_ = 180, accel_ = 240, cruise_ = 20;
    float pos_ = 90, target_ = 90, vel_ = 0;
    bool active_ = false;
};

// ---------------------------------------------------------------------------
// History of the servo angle, so a frame captured at time T can be paired with
// the angle the camera really had at T (inference/Wi-Fi latency is ~1 s, during
// which the servo keeps sweeping).
// ---------------------------------------------------------------------------
class ServoHistory {
public:
    static constexpr int N = 256; // 256 x 20 ms = 5.1 s

    void push(uint32_t ms, float deg) {
        head_ = (head_ + 1) % N;
        t_[head_] = ms; d_[head_] = deg;
        if (count_ < N) ++count_;
    }
    // Interpolated angle at time `ms`. false if `ms` is older than the buffer.
    bool at(uint32_t ms, float& deg) const {
        if (!count_) return false;
        if (int32_t(ms - t_[head_]) >= 0) { deg = d_[head_]; return true; } // newer than newest
        for (int i = 0; i < count_ - 1; ++i) {
            const int a = (head_ - i - 1 + 2 * N) % N; // older sample
            const int b = (head_ - i + N) % N;         // newer sample
            if (int32_t(ms - t_[a]) >= 0) {
                const float span = float(int32_t(t_[b] - t_[a]));
                const float f = span > 0 ? float(int32_t(ms - t_[a])) / span : 1.f;
                deg = d_[a] + (d_[b] - d_[a]) * std::min(1.f, std::max(0.f, f));
                return true;
            }
        }
        return false;
    }
private:
    uint32_t t_[N] = {};
    float d_[N] = {};
    int head_ = 0, count_ = 0;
};

// ---------------------------------------------------------------------------
// Speeds shared by the drivers and the FSM (TV3 values). servoMoveDeg() cruises at
// kServoMoveDps with kServoAccelDps2 soft start/stop.
// ---------------------------------------------------------------------------
#ifndef SWEEP_SERVO_MOVE_DPS
#define SWEEP_SERVO_MOVE_DPS 60.0f
#endif
#ifndef SWEEP_SERVO_ACCEL_DPS2
#define SWEEP_SERVO_ACCEL_DPS2 240.0f
#endif
constexpr float kServoMoveDps = SWEEP_SERVO_MOVE_DPS;
constexpr float kServoAccelDps2 = SWEEP_SERVO_ACCEL_DPS2;

// Duration (ms) of a ServoMotion move of `dist` deg from rest to rest: trapezoid
// (accelerate, cruise, brake) or triangle when the cruise speed is never reached.
inline uint32_t servoTravelMs(float dist, float cruiseDps = kServoMoveDps,
                              float accelDps2 = kServoAccelDps2) {
    dist = std::fabs(dist);
    if (dist < 0.02f) return 0;
    const float v = std::max(1.f, cruiseDps), a = std::max(1.f, accelDps2);
    const float t = dist > v * v / a ? dist / v + v / a : 2.f * std::sqrt(dist / a);
    return uint32_t(std::ceil(t * 1000.f)) + 20;  // + one 20 ms driver tick
}
