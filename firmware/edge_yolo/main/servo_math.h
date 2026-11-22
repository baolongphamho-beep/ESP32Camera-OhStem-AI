#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

static constexpr uint32_t kServoPwmFreqHz = 50U;
static constexpr uint32_t kServoDutyResolutionBits = 14U;
static constexpr float kServoMinPulseUs = 500.0f;
static constexpr float kServoMaxPulseUs = 2500.0f;
static constexpr float kServoHardMinDeg = 0.0f;
static constexpr float kServoHardMaxDeg = 180.0f;

static constexpr int8_t kServoDefaultPin = 10;       // YOLO UNO D7
static constexpr float kServoDefaultMinDeg = 45.0f;   // Mechanical safety lower bound (deg)
static constexpr float kServoDefaultMaxDeg = 135.0f;  // Mechanical safety upper bound (deg)
static constexpr float kServoDefaultLevelDeg = 90.0f; // Servo angle where camera is horizontal (0 deg tilt)
static constexpr float kServoDefaultBottomDeg = 112.0f; // Looking down (+22 deg tilt with dir=+1)
static constexpr float kServoDefaultTopDeg = 85.0f;     // Looking up (-5 deg tilt with dir=+1)
static constexpr float kServoDefaultStepDeg = 4.5f;
static constexpr int8_t kServoDefaultDir = 1;         // +1: higher servo deg tilts camera DOWN (TIANKONGRC 20kg 180-deg)
static constexpr uint16_t kServoDefaultSettleMs = 200U;

static constexpr float kServoRampMaxStepDeg = 2.0f;
static constexpr uint32_t kServoRampStepDelayMs = 20U;

inline bool isValidServoPin(int gpioPin) {
    if (gpioPin < 0 || gpioPin > 48) return false;
    if (gpioPin == 11 || gpioPin == 12) return false; // I2C LCD (SDA=11, SCL=12)
    if (gpioPin == 19 || gpioPin == 20) return false; // Native USB Serial/JTAG
    if (gpioPin >= 22 && gpioPin <= 25) return false; // Unbonded on ESP32-S3
    if (gpioPin >= 26 && gpioPin <= 37) return false; // Octal Flash / PSRAM
    if (gpioPin == 43 || gpioPin == 44) return false; // UART0 console
    if (gpioPin == 45 || gpioPin == 46) return false; // RGB LED / strapping
    return true;
}

inline float clampServoDeg(float deg, float minDeg, float maxDeg) {
    float lo = minDeg;
    float hi = maxDeg;
    if (!std::isfinite(lo) || !std::isfinite(hi)) {
        lo = kServoDefaultMinDeg;
        hi = kServoDefaultMaxDeg;
    } else {
        lo = std::clamp(lo, kServoHardMinDeg, kServoHardMaxDeg);
        hi = std::clamp(hi, kServoHardMinDeg, kServoHardMaxDeg);
        if (lo > hi) {
            lo = kServoDefaultMinDeg;
            hi = kServoDefaultMaxDeg;
        }
    }
    if (!std::isfinite(deg)) {
        return 0.5f * (lo + hi);
    }
    return std::clamp(deg, lo, hi);
}

inline float servoDegToPulseUs(float deg,
                               float minPulseUs = kServoMinPulseUs,
                               float maxPulseUs = kServoMaxPulseUs) {
    const float loUs = (std::isfinite(minPulseUs) && minPulseUs > 0.0f) ? minPulseUs : kServoMinPulseUs;
    const float hiUs = (std::isfinite(maxPulseUs) && maxPulseUs > loUs) ? maxPulseUs : kServoMaxPulseUs;
    const float clamped = std::isfinite(deg)
        ? std::clamp(deg, kServoHardMinDeg, kServoHardMaxDeg)
        : kServoDefaultLevelDeg;
    return loUs + (clamped / kServoHardMaxDeg) * (hiUs - loUs);
}

inline uint32_t servoPulseUsToDuty(float pulseUs,
                                   uint32_t freqHz = kServoPwmFreqHz,
                                   uint32_t resolutionBits = kServoDutyResolutionBits) {
    if (!std::isfinite(pulseUs) || pulseUs <= 0.0f || freqHz == 0U ||
        resolutionBits == 0U || resolutionBits > 20U) {
        return 0U;
    }
    const uint32_t scale = 1U << resolutionBits;
    const uint32_t maxDuty = scale - 1U;
    const double periodUs = 1000000.0 / static_cast<double>(freqHz);
    const long rounded = std::lround((static_cast<double>(pulseUs) / periodUs) * static_cast<double>(scale));
    if (rounded <= 0L) return 0U;
    if (static_cast<uint64_t>(rounded) >= maxDuty) return maxDuty;
    return static_cast<uint32_t>(rounded);
}

inline float servoDegToTiltDeg(float servoDeg,
                               float levelDeg,
                               int8_t dir,
                               float tiltOffsetDeg = 0.0f) {
    if (!std::isfinite(servoDeg) || !std::isfinite(levelDeg) || !std::isfinite(tiltOffsetDeg)) {
        return 0.0f;
    }
    const float sign = (dir >= 0) ? 1.0f : -1.0f;
    const float tilt = tiltOffsetDeg + sign * (servoDeg - levelDeg);
    return std::clamp(tilt, -79.0f, 79.0f);
}

inline uint32_t computeServoRampSteps(float fromDeg,
                                      float toDeg,
                                      float maxStepDeg = kServoRampMaxStepDeg) {
    if (!std::isfinite(fromDeg) || !std::isfinite(toDeg) ||
        !std::isfinite(maxStepDeg) || maxStepDeg <= 0.0f) {
        return 1U;
    }
    const float delta = std::fabs(toDeg - fromDeg);
    if (delta < 0.05f) return 0U;
    return std::max<uint32_t>(1U, static_cast<uint32_t>(std::ceil(delta / maxStepDeg)));
}

inline bool servoNextSweepDeg(float currentDeg,
                              float bottomDeg,
                              float topDeg,
                              float stepDeg,
                              float& nextDeg) {
    if (!std::isfinite(currentDeg) || !std::isfinite(bottomDeg) ||
        !std::isfinite(topDeg) || !std::isfinite(stepDeg) || stepDeg <= 0.0f) {
        return false;
    }
    const float sweepDir = (topDeg >= bottomDeg) ? 1.0f : -1.0f;
    if (sweepDir * (topDeg - currentDeg) <= 0.05f) {
        return false;
    }
    const float candidate = currentDeg + sweepDir * stepDeg;
    nextDeg = (sweepDir > 0.0f) ? std::min(candidate, topDeg) : std::max(candidate, topDeg);
    return true;
}
