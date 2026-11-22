#include <cassert>
#include <cmath>
#include <limits>
#include "../firmware/edge_yolo/main/servo_math.h"

int main() {
    // Pin validation accepts free YOLO UNO GPIOs and rejects reserved system pins.
    assert(isValidServoPin(10));
    assert(isValidServoPin(6));
    assert(isValidServoPin(17));
    assert(isValidServoPin(18));
    assert(isValidServoPin(21));
    assert(isValidServoPin(38));

    assert(!isValidServoPin(-1));
    assert(!isValidServoPin(11)); // I2C SDA
    assert(!isValidServoPin(12)); // I2C SCL
    assert(!isValidServoPin(19)); // USB D-
    assert(!isValidServoPin(20)); // USB D+
    for (int pin = 22; pin <= 37; ++pin) {
        assert(!isValidServoPin(pin)); // Unbonded + Octal Flash/PSRAM
    }
    assert(!isValidServoPin(43)); // UART0 TX
    assert(!isValidServoPin(44)); // UART0 RX
    assert(!isValidServoPin(45)); // RGB LED
    assert(!isValidServoPin(46)); // Strapping
    assert(!isValidServoPin(49));

    // Mechanical angle clamping and invalid input fallback.
    assert(std::fabs(clampServoDeg(90.0f, 40.0f, 140.0f) - 90.0f) < 0.001f);
    assert(std::fabs(clampServoDeg(10.0f, 40.0f, 140.0f) - 40.0f) < 0.001f);
    assert(std::fabs(clampServoDeg(175.0f, 40.0f, 140.0f) - 140.0f) < 0.001f);
    assert(std::fabs(clampServoDeg(std::numeric_limits<float>::quiet_NaN(), 40.0f, 140.0f) - 90.0f) < 0.001f);
    assert(std::fabs(clampServoDeg(100.0f, 140.0f, 40.0f) - 100.0f) < 0.001f); // Inverted bounds fall back to 40..140

    // Pulse width (500..2500 us) and 50 Hz 14-bit duty conversion.
    assert(std::fabs(servoDegToPulseUs(0.0f) - 500.0f) < 0.001f);
    assert(std::fabs(servoDegToPulseUs(90.0f) - 1500.0f) < 0.001f);
    assert(std::fabs(servoDegToPulseUs(180.0f) - 2500.0f) < 0.001f);
    assert(servoPulseUsToDuty(500.0f, 50U, 14U) == 410U);
    assert(servoPulseUsToDuty(1500.0f, 50U, 14U) == 1229U);
    assert(servoPulseUsToDuty(2500.0f, 50U, 14U) == 2048U);
    assert(servoPulseUsToDuty(-10.0f, 50U, 14U) == 0U);
    assert(servoPulseUsToDuty(std::numeric_limits<float>::infinity(), 50U, 14U) == 0U);

    // Servo angle -> optical tilt (positive tilt = camera tilted DOWN).
    assert(std::fabs(servoDegToTiltDeg(68.0f, 90.0f, -1) - 22.0f) < 0.001f);
    assert(std::fabs(servoDegToTiltDeg(90.0f, 90.0f, -1) - 0.0f) < 0.001f);
    assert(std::fabs(servoDegToTiltDeg(95.0f, 90.0f, -1) - (-5.0f)) < 0.001f);

    assert(std::fabs(servoDegToTiltDeg(112.0f, 90.0f, 1) - 22.0f) < 0.001f);
    assert(std::fabs(servoDegToTiltDeg(85.0f, 90.0f, 1) - (-5.0f)) < 0.001f);
    assert(std::fabs(servoDegToTiltDeg(90.0f, 90.0f, -1, 1.5f) - 1.5f) < 0.001f);
    assert(std::fabs(servoDegToTiltDeg(0.0f, 90.0f, -1) - 79.0f) < 0.001f); // Clamped below 80 deg

    // Smooth ramp step count and 50 Hz frame-aligned step delay.
    assert(kServoRampStepDelayMs == 20U);
    assert(computeServoRampSteps(90.0f, 90.02f, 2.0f) == 0U);
    assert(computeServoRampSteps(65.0f, 95.0f, 2.0f) == 15U);
    assert(computeServoRampSteps(95.0f, 65.0f, 2.0f) == 15U);
    assert(computeServoRampSteps(90.0f, 90.5f, 2.0f) == 1U);

    // Ascending sweep (bottom=68 -> top=95, step=4.5).
    float cur = 68.0f;
    float next = 0.0f;
    int ascSteps = 0;
    while (servoNextSweepDeg(cur, 68.0f, 95.0f, 4.5f, next)) {
        assert(next > cur && next <= 95.001f);
        cur = next;
        ++ascSteps;
    }
    assert(ascSteps == 6 && std::fabs(cur - 95.0f) < 0.001f);

    // Descending sweep (bottom=112 -> top=85, step=4.5).
    cur = 112.0f;
    int descSteps = 0;
    while (servoNextSweepDeg(cur, 112.0f, 85.0f, 4.5f, next)) {
        assert(next < cur && next >= 84.999f);
        cur = next;
        ++descSteps;
    }
    assert(descSteps == 6 && std::fabs(cur - 85.0f) < 0.001f);
    assert(!servoNextSweepDeg(85.0f, 112.0f, 85.0f, 4.5f, next));
    return 0;
}
