#pragma once
#include "measurement_state.h"

void updateCameraMeasurement(PersonStatus status, float heightCm = 0, float distanceM = 0);
void updateWeightMeasurement(float weightKg, bool stable);
void clearWeightMeasurement();
MeasurementView measurementView();
