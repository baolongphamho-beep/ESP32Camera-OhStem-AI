#pragma once
#include "measurement_state.h"
#include "pedestrian_state.h"

void updateCameraMeasurement(PersonStatus status, float heightCm = 0, float distanceM = 0);
void updateWeightMeasurement(float weightKg, bool stable);
void clearWeightMeasurement();
MeasurementView measurementView();
// True if the scale sent any weight packet within windowMs, even with no camera.
bool weightReceivedWithin(uint32_t windowMs);
// Result hold (TV5-1): keep H/W/BMI after stepping off. 0 = off (default).
void setResultHoldMs(uint32_t ms);
ResultHold heldResult();
void clearResultHold();
// Project 3 pedestrian distance/warning from pedestrian_distance.py.
void updatePedestrian(float distanceM, bool warning);
void clearPedestrian();
PedestrianView pedestrianView();
