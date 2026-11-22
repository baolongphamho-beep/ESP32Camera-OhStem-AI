#include <Arduino.h>
#include "measurements.h"

static MeasurementState measurements;
static portMUX_TYPE measurementMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t lastWeightPacketMs = 0;
static bool anyWeightPacket = false;

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
    lastWeightPacketMs = now;
    anyWeightPacket = true;
    portEXIT_CRITICAL(&measurementMux);
}

void clearWeightMeasurement() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&measurementMux);
    measurements.clearWeight(now);
    portEXIT_CRITICAL(&measurementMux);
}

MeasurementView measurementView() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&measurementMux);
    const MeasurementView out = measurements.view(now);
    portEXIT_CRITICAL(&measurementMux);
    return out;
}

void setResultHoldMs(uint32_t ms) {
    portENTER_CRITICAL(&measurementMux);
    measurements.setResultHoldMs(ms);
    portEXIT_CRITICAL(&measurementMux);
}

ResultHold heldResult() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&measurementMux);
    const ResultHold out = measurements.heldResult(now);
    portEXIT_CRITICAL(&measurementMux);
    return out;
}

void clearResultHold() {
    portENTER_CRITICAL(&measurementMux);
    measurements.clearResultHold();
    portEXIT_CRITICAL(&measurementMux);
}

static PedestrianState pedestrian;

void updatePedestrian(float distanceM, bool warning) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&measurementMux);
    pedestrian.update(now, distanceM, warning);
    portEXIT_CRITICAL(&measurementMux);
}

void clearPedestrian() {
    portENTER_CRITICAL(&measurementMux);
    pedestrian.clear();
    portEXIT_CRITICAL(&measurementMux);
}

PedestrianView pedestrianView() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&measurementMux);
    const PedestrianView out = pedestrian.view(now);
    portEXIT_CRITICAL(&measurementMux);
    return out;
}

bool weightReceivedWithin(uint32_t windowMs) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&measurementMux);
    const bool recent = anyWeightPacket && now - lastWeightPacketMs <= windowMs;
    portEXIT_CRITICAL(&measurementMux);
    return recent;
}
