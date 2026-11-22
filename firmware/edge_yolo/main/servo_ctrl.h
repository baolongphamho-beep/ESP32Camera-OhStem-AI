#pragma once
// Tilt-servo driver for the OhStem camera (ESP32-S3, ESP-IDF LEDC, 50 Hz / 14-bit).
// Owner: TV3 (servo_ctrl.zip, 22/11/2026), merged into the LAB Plan 2 firmware by TV5.
// Interface Contract functions are in lib/EdgeSweep/servo_ctrl.h; these are the extras.
#include "../../../lib/EdgeSweep/servo_ctrl.h"

enum class ServoState : uint8_t {
    Detached,   // PWM stopped (standby): servo is unpowered-limp, no holding current
    Holding,    // PWM running, position held
    Moving,     // point-to-point move (servoMoveDeg / homing)
    Sweeping,   // continuous min -> max sweep in progress
    SweepDone,  // reached max without being stopped
};

// ---- Continuous sweep with instant stop (not used by the Plan 2 step-scan FSM) ----
// Go to minDeg (if not already there) and then sweep min -> max at `speedDps`
// (servo_dps) without blocking.
bool  servoSweepStart(float speedDps);
// IMMEDIATE stop: the setpoint freezes at the current angle (<= one 20 ms tick) and
// the servo keeps holding there. Returns the angle it froze at.
float servoStop();

float      servoDeg();               // current commanded (moving) angle
ServoState servoState();
// Angle the camera had at time `ms` (clockMs() base), interpolated from a 5 s history.
bool       servoDegAt(uint32_t ms, float& deg);
// clockMs() since which the servo has been motionless (0 while it is moving).
uint32_t   servoStillSinceMs();
// clockMs() at which the current/last continuous sweep actually began (after homing).
uint32_t   servoSweepStartedAtMs();
// Before the FIRST servoInit() after boot the arm position is unknown and TV3's driver
// assumes minDeg (one full-speed snap there). Manual jog calls this with the angle the
// user asked for, so the only snap goes to that angle instead. No effect once known.
void       servoAssumeAt(float deg);
