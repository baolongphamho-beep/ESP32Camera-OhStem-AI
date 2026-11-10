#include <Arduino.h>
#include "measurements.h"

static MeasurementState measurements;
static portMUX_TYPE measurementMux = portMUX_INITIALIZER_UNLOCKED;

void updateCameraMeasurement(PersonStatus status, float heightCm, float distanceM) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&measurementMux);
    measurements.camera(now, status, heightCm, distanceM);
    portEXIT_CRITICAL(&measurementMux);
}

void updateWeightMeasurement(float weightKg, bool stable) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&measurementMux);
    measurements.weight(now, weightKg, stable);
    portEXIT_CRITICAL(&measurementMux);
}

void clearWeightMeasurement() {
    portENTER_CRITICAL(&measurementMux);
    measurements.clearWeight();
    portEXIT_CRITICAL(&measurementMux);
}

MeasurementView measurementView() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&measurementMux);
    const MeasurementView out = measurements.view(now);
    portEXIT_CRITICAL(&measurementMux);
    return out;
}
