#pragma once
// Tilt-servo driver for the OhStem camera (ESP32-S3, ESP-IDF LEDC, 50 Hz / 14-bit).
// Owner: TV3. All angles are servo-horn degrees (0..180) unless named "tilt".
#include <cstdint>
#include "edge_io.h"

enum class ServoState : uint8_t {
    Detached,   // PWM stopped (standby): servo is unpowered-limp, no holding current
    Holding,    // PWM running, position held
    Moving,     // point-to-point move (servoMoveDeg / homing)
    Sweeping,   // continuous min -> max sweep in progress
    SweepDone,  // reached max without being stopped
};

// ---- Interface contract (PLAN_2, section 3) --------------------------------
// Assumes the servo is physically at minDeg (rest/bottom) when called. The first
// PWM write snaps to minDeg once; power the servo from the external 5 V supply.
bool  servoInit(int gpioPin, int minDeg, int maxDeg);
// Smooth blocking move (clamped to [min,max]), then waits settleMs.
void  servoMoveDeg(float deg, uint32_t settleMs);
// Stop PWM entirely (call once the servo has stopped, to remove jitter/heat).
void  servoDetach();
// Servo angle -> real camera pitch. README convention: DOWN positive, UP negative.
float servoToTiltDeg(float servoDeg, const EdgeConfig& cfg);

// ---- Continuous sweep with instant stop ------------------------------------
// Go to minDeg (fast, if not already there) and then sweep min -> max at
// `speedDps` without blocking. Returns immediately.
bool  servoSweepStart(float speedDps);
// IMMEDIATE stop: the setpoint freezes at the current angle (<= one 20 ms tick)
// and the servo keeps holding there. Safe to call from any task. Returns the
// angle it froze at.
float servoStop();

float      servoDeg();               // current commanded angle
ServoState servoState();
// Angle the camera had at time `ms` (clockMs() base), interpolated from a 5 s
// history. Use it with the frame's capture time: inference latency is ~1 s, during
// which the servo moved on. false if `ms` is older than the history.
bool       servoDegAt(uint32_t ms, float& deg);
// clockMs() since which the servo has been motionless (0 while it is moving).
// A frame is "stationary" (exact angle, no motion blur) if it was captured at or
// after servoStillSinceMs() + settle time.
uint32_t   servoStillSinceMs();
// clockMs() at which the current/last sweep actually began (after homing). Frames
// captured before this are not part of the sweep.
uint32_t   servoSweepStartedAtMs();
