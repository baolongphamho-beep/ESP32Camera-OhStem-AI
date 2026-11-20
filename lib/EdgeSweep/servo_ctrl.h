#pragma once
// Plan 2 Interface Contract 3.2 - servo driver (owner TV3).
// PlatformIO/Arduino version (ESP32Servo, 50 Hz, 500-2400 us over 0-180 deg) used by
// env:yolo_uno_servo_sweep; the ESP-IDF firmware/edge_yolo version (esp_driver_ledc)
// must keep the same four functions.
//
// servoMoveDeg() does NOT block: it writes the pulse and remembers when the servo
// should be settled (servoSettled()). The FSM (edge_fsm.h) already ignores camera
// frames until servo_settle ms after each move, so loop() keeps reading UART/USB.
#include <cstdint>

#include "edge_config.h"
#include "edge_geometry.h"

bool servoInit(int gpioPin, int minDeg, int maxDeg);  // Attach (50 Hz); false if the pin is invalid.
void servoMoveDeg(float deg, uint32_t settleMs);       // Clamped to [minDeg, maxDeg].
void servoDetach();                                    // Stop the pulse: no holding torque, no buzz.
bool servoSettled();                                   // settleMs passed since the last real move.
bool servoAttached();
float servoCommandDeg();                               // Last command written (NAN if none).

// Servo command -> real camera tilt (deg below horizontal, +down), from servo_level/servo_dir.
// Pure, header-only so the FSM and the PC tests use exactly the same conversion.
inline float servoToTiltDeg(float servoDeg, const EdgeConfig& cfg) {
    return edge_geometry::tiltDownDeg(servoDeg, cfg.levelDeg, cfg.dir);
}
