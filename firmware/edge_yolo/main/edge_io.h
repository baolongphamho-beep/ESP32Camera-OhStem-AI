#pragma once
#include <cstddef>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "measurement_state.h"
#include "scale_presence.h"
#include "servo_math.h"

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
    uint16_t holdSec = 10; // RESULT_HOLD duration in seconds (0 disables hold, max 60).
    int8_t servoPin = kServoDefaultPin;
    float servoMinDeg = kServoDefaultMinDeg;
    float servoMaxDeg = kServoDefaultMaxDeg;
    float servoBottomDeg = kServoDefaultBottomDeg;
    float servoTopDeg = kServoDefaultTopDeg;
    float servoStepDeg = kServoDefaultStepDeg;
    float servoLevelDeg = kServoDefaultLevelDeg;
    int8_t servoDir = kServoDefaultDir;
    uint16_t servoSettleMs = kServoDefaultSettleMs;
    uint32_t revision = 0;
};

void edgeIoInit();
EdgeConfig configSnapshot();
void saveFocalIfUnchanged(const EdgeConfig& original, float focal);
bool wifiReady();
void wifiIp(char* out, size_t size); // UNO's own IPv4, or "no IP".
int wifiRssi();
void cameraState(PersonStatus status, float height = 0, float distance = 0);
bool cameraStateForScaleSession(uint32_t session, PersonStatus status, float height,
                                float distance, MeasurementView& out);
void clearMeasurementHold();
MeasurementView stateSnapshot();
MeasurementView scaleSnapshot();
ScaleSample scaleSampleSnapshot();
void lcdHoldResult(const MeasurementView* result);
// Call from the application task, never from a BLE callback or ISR.
void setPowerMode(bool active, bool keepLcdOn = false);
bool powerModeActive();
struct EdgePowerView {
    bool pmEnabled = false;
    bool lightSleepConfigured = false;
    bool usbSleepBlocked = false;
    bool wifiStarted = false;
    bool lcdBacklight = false;
    int cpuMhz = 0;
    int minCpuMhz = 0;
    int maxCpuMhz = 0;
    int noLightSleepLocks = -1;
    const char* wifiPowerSave = "UNKNOWN";
};
EdgePowerView powerSnapshot();
void bleSetLowPowerScan(bool standby);
float bleScanDutyPercent();
void lcdSetBacklight(bool on);
bool usbConnected();
uint32_t wifiConnectEpoch();
void edgeNotifyControl();
bool edgeWaitControl(uint32_t timeoutMs);
using UsbLineHandler = void (*)(const char* line);
void setUsbLineHandler(UsbLineHandler handler);
void usbWriteText(const char* text);
void usbFrame(const uint8_t* jpeg, size_t length, const char* metadata);
void lcdMessage(const char* first, const char* second);
uint32_t clockMs();
