#pragma once
#include <cstddef>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "measurement_state.h"
#include "edge_device_config.h"
#include "edge_scale.h"
#include "../../../lib/EdgeSweep/edge_io.h"
void edgeIoInit();
EdgeDeviceConfig configSnapshot();
ScaleReading scaleSnapshot();
bool wifiReady();
int wifiRssi();  // dBm of the camera AP, 0 = not connected
bool cameraTestEnabled();
bool servoJogTarget(float& deg);  // USB "SERVO <deg>" lease active -> target (servo_jog.h)
void bleSetCameraTest(bool enabled);
void wifiIp(char* out, size_t size);
void setPowerMode(bool active);
void bleSetLowPowerScan(bool standby);
void bleSetCapturePriority(bool capturing);
void cameraState(PersonStatus status, float height = 0, float distance = 0);
MeasurementView stateSnapshot();
void usbFrame(const uint8_t* jpeg, size_t length, const char* metadata);
void lcdMessage(const char* first, const char* second);
uint32_t clockMs();
