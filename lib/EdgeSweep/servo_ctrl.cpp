// Arduino (ESP32Servo) implementation of servo_ctrl.h for env:yolo_uno_servo_sweep.
// Same motion as the native TV3 driver (servo_motion.h trapezoid, 60 deg/s, 240 deg/s^2),
// advanced from loop(): driveServo() calls servoMoveDeg() every loop, which steps it.
#ifdef SERVO_SWEEP_MODE
#include "servo_ctrl.h"

#include <Arduino.h>
#include <ESP32Servo.h>

#include "config.h"
#include "servo_motion.h"

static Servo servo;
static int attachedPin = -1;
static ServoMotion motion;
static bool known = false;       // Position kept across detach (gears hold the arm).
static float written = NAN;      // Pulse currently on the pin (avoid rewriting).
static uint32_t lastStepMs = 0, stillSince = 0, settleMs = 0;

static void writeDeg(float deg) {
    if (!std::isfinite(deg) || (std::isfinite(written) && std::fabs(written - deg) < 0.05f)) return;
    const float us = SWEEP_SERVO_MIN_US + (float)(SWEEP_SERVO_MAX_US - SWEEP_SERVO_MIN_US) * deg / 180.0f;
    servo.writeMicroseconds((int)lroundf(us));
    written = deg;
}

bool servoInit(int gpioPin, int minDeg, int maxDeg) {
    // No reserved-pin blocking (team decision 22/11).
    if (gpioPin < 0 || gpioPin > 48 || minDeg >= maxDeg) return false;
    // Hard travel limits (edge_config.h, 45..135 by default) win over the caller's range.
    const float lo = minDeg < SWEEP_LIMIT_MIN_DEG ? SWEEP_LIMIT_MIN_DEG : (float)minDeg;
    const float hi = maxDeg > SWEEP_LIMIT_MAX_DEG ? SWEEP_LIMIT_MAX_DEG : (float)maxDeg;
    if (hi - lo < 1.0f) return false;
    static bool timerReady = false;
    if (!timerReady) { ESP32PWM::allocateTimer(0); timerReady = true; }
    const float last = motion.pos();
    motion.configure(lo, hi, kServoAccelDps2);
    if (attachedPin == gpioPin) return true;
    if (attachedPin >= 0) servo.detach();
    servo.setPeriodHertz(50);
    servo.attach(gpioPin, SWEEP_SERVO_MIN_US, SWEEP_SERVO_MAX_US);  // Returns a channel (0 is valid).
    if (!servo.attached()) {
        attachedPin = -1;
        return false;
    }
    attachedPin = gpioPin;
    written = NAN;
    motion.reset(known ? last : lo);  // First attach after boot: assume the arm rests at bottom.
    known = true;
    lastStepMs = millis();
    stillSince = lastStepMs;
    writeDeg(motion.pos());
    return true;
}

void servoUpdate() {
    if (attachedPin < 0) return;
    const uint32_t now = millis();
    float dt = (float)(now - lastStepMs) / 1000.0f;
    lastStepMs = now;
    if (dt > 0.08f) dt = 0.08f;  // Same clamp as the native 20 ms tick.
    if (motion.moving() && motion.step(dt)) stillSince = now;
    writeDeg(motion.pos());
}

void servoMoveDeg(float deg, uint32_t settle) {
    if (attachedPin < 0 || !std::isfinite(deg)) return;
    if (std::fabs(motion.clampDeg(deg) - motion.target()) >= 1e-3f) {
        motion.setTarget(deg, kServoMoveDps);
        settleMs = settle;
        stillSince = motion.moving() ? 0 : millis();
    }
    servoUpdate();
}

void servoDetach() {
    if (attachedPin >= 0) servo.detach();
    attachedPin = -1;
    motion.stop();
    written = NAN;
}

bool servoSettled() {
    return attachedPin >= 0 && !motion.moving() && stillSince != 0 && millis() - stillSince >= settleMs;
}
bool servoAttached() { return attachedPin >= 0; }
float servoCommandDeg() { return attachedPin >= 0 ? motion.target() : NAN; }
#endif
