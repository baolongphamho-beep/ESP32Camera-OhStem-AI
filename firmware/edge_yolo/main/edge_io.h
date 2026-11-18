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
};

void edgeIoInit();
EdgeConfig configSnapshot();
void saveFocalIfUnchanged(const EdgeConfig& original, float focal);
bool wifiReady();
void cameraState(PersonStatus status, float height = 0, float distance = 0);
MeasurementView stateSnapshot();
MeasurementView scaleSnapshot();
// Call from the application task, never from a BLE callback or ISR.
void setPowerMode(bool active);
bool powerModeActive();
struct EdgePowerView {
    bool pmEnabled = false;
    bool lightSleepConfigured = false;
    bool usbSleepBlocked = false;
    bool wifiStarted = false;
    int cpuMhz = 0;
    int minCpuMhz = 0;
    int maxCpuMhz = 0;
    int noLightSleepLocks = -1;
    const char* wifiPowerSave = "UNKNOWN";
};
EdgePowerView powerSnapshot();
void bleSetLowPowerScan(bool standby);
unsigned bleScanDutyPercent();
void lcdSetBacklight(bool on);
void usbFrame(const uint8_t* jpeg, size_t length, const char* metadata);
void lcdMessage(const char* first, const char* second);
uint32_t clockMs();
