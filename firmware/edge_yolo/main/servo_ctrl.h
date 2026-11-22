#pragma once
#include <cstdint>
#include "edge_io.h"

bool servoInit(int gpioPin, int minDeg, int maxDeg);
bool servoInitFromConfig(const EdgeConfig& cfg);
void servoMoveDeg(float deg, uint32_t settleMs);
void servoDetach();
bool servoIsInitialized();
bool servoIsAttached();
float servoCurrentDeg();
float servoToTiltDeg(float servoDeg, const EdgeConfig& cfg);
