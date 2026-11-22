#pragma once
#include <cstddef>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "measurement_state.h"

struct EdgeConfig {
    char ssid[33] = {};
    char password[65] = {};
    char cameraUrl[160] = "http://10.127.7.68/capture";
    char scaleMac[18] = "70:87:9e:ea:3a:e0";
    float distanceM = 3;
    float referenceCm = 183;
    float focalPx = 0; // Normalized fy / image height, NOT millimetres or raw pixels.
    float cameraHeightCm = 0;
    float tiltDeg = 0;
    bool groundMode = false;
    // Tilt servo (TV3). Servo-horn degrees 0..180. NVS keys: servo_pin, servo_bottom,
    // servo_top, servo_level, servo_dir, servo_settle, servo_dps, servo_step, servo_delay.
    int servoPin = 10;            // GPIO10 / D7
    float servoMinDeg = 45;       // sweep START = bottom (camera looks down). Keep inside mechanical stops.
    float servoMaxDeg = 135;      // sweep END   = top    (camera looks up)
    float servoLevelDeg = 90;     // servo angle at which the camera is exactly horizontal (measure it!)
    int servoDir = -1;            // -1: larger servo angle tilts camera UP; +1: DOWN (see servo_motion.h)
    uint32_t servoSettleMs = 200; // wait after a stop before a frame counts as "stationary"
    float servoSpeedDps = 4;      // only for the continuous servoSweepStart() API (unused by the step-scan)
    float servoStepDeg = 5;       // step-scan: angle added when no full-body person was found
    uint32_t servoFrameDelayMs = 500; // camera returns images ~this old; wait it out before trusting a frame
};

void edgeIoInit();
EdgeConfig configSnapshot();
void saveFocalIfUnchanged(const EdgeConfig& original, float focal);
bool wifiReady();
void wifiIp(char* out, size_t size); // UNO's own IPv4, or "no IP".
void bleSetCapturePriority(bool capturing);
void cameraState(PersonStatus status, float height = 0, float distance = 0);
MeasurementView stateSnapshot();
void usbFrame(const uint8_t* jpeg, size_t length, const char* metadata);
void lcdMessage(const char* first, const char* second);
uint32_t clockMs();
