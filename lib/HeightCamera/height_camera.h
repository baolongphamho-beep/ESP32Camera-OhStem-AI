#pragma once

void initHeightCamera();

void runHeightCameraTask(void *pvParameters);

float getServoCommandDeg();

float getOpticalAngleDeg();

float getFootAngleDeg();

float getHeadAngleDeg();

float getLastHeightCm();

bool hasValidHeight();

// Thread-safe: the reset itself runs inside runHeightCameraTask.
void requestHeightReset();

// millis() of the last camera UART message; 0 if none received yet.
unsigned long lastHeightCameraMessageMs();
