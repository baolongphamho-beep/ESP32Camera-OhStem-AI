#pragma once
// Plan 2 Interface Contract 3.2 - servo driver (owner TV3).
// Two implementations with the same functions:
//   - PlatformIO/Arduino (ESP32Servo) lib/EdgeSweep/servo_ctrl.cpp, env:yolo_uno_servo_sweep;
//   - ESP-IDF LEDC firmware/edge_yolo/main/servo_ctrl.cpp (TV3 servo_ctrl.zip, 22/11), which
//     adds continuous sweep / instant stop / angle history (firmware/edge_yolo/main/servo_ctrl.h).
// Motion: servo_motion.h trapezoid (kServoMoveDps = 60 deg/s, 240 deg/s^2), no jerk on the
// camera cable. All angles are servo-horn degrees (0..180) unless named "tilt".
//
// servoMoveDeg() does NOT block (the FSM loop must keep reading scale/USB): it sets the
// target; servoSettled() becomes true settleMs after the servo STOPPED there. The FSM
// (edge_fsm.h) independently waits servoTravelMs + servo_settle + servo_delay before it
// trusts a camera frame (TV3 servoFrameFresh()).
// NOTE (TV1): the FSM moves to servo_bottom on WAKING_HOMING and calls servoDetach() in
// STANDBY / RESULT_HOLD (src/app_main.cpp driveServo(), firmware app_main.cpp driveServo()).
#include <cstdint>

#include "edge_config.h"
#include "edge_geometry.h"

bool servoInit(int gpioPin, int minDeg, int maxDeg);  // Attach (50 Hz); starts at the last known angle.
void servoMoveDeg(float deg, uint32_t settleMs);       // Clamped to [minDeg, maxDeg]; non-blocking.
void servoDetach();                                    // Stop the pulse: no holding torque, no buzz.
void servoUpdate();                                    // Advance the motion (Arduino; no-op on native).
bool servoSettled();                                   // Stopped at the target for settleMs.
bool servoAttached();
float servoCommandDeg();                               // Motion target (NAN if detached/none).

// Servo command -> real camera tilt (deg below horizontal, +down), from servo_level/servo_dir.
// TV3 convention: tilt = dir * (servoDeg - level); dir -1 = larger angle tilts UP.
// Pure, header-only so the FSM and the PC tests use exactly the same conversion.
inline float servoToTiltDeg(float servoDeg, const EdgeConfig& cfg) {
    return edge_geometry::tiltDownDeg(servoDeg, cfg.levelDeg, cfg.dir);
}
