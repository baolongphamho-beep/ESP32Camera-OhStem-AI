// Servo-tilt height + Mi Scale 2 weight (BLE) + BMI on LCD 16x2.
// Compiled only by env:yolo_uno_height_servo_bmi; empty in every other env.
//
// Flow: pedestrian_detector.py (auto) or manual_height_test.py (manual) drives
// the servo and marks foot/head -> height_camera computes H. The scale task
// reports weight. BMI = W / (H/100)^2 once H is valid and the weight is locked.
#ifdef HEIGHT_SERVO_BMI_MODE
#include <Arduino.h>
#include <strings.h>

#include "config.h"
#include "display.h"
#include "height_camera.h"
#include "height_config.h"
#include "measurements.h"
#include "scale.h"
#include "scale_power.h"

// Keep the scale powered while the camera sends commands or the scale sends weight.
static constexpr uint32_t ACTIVITY_WINDOW_MS = 10000;

// Feed the servo height into the shared measurement state. The state expires
// camera data after MEASUREMENT_CAMERA_TTL_MS, so this is refreshed often.
static void publishHeight() {
    static uint32_t lastPublish = 0;
    if (millis() - lastPublish < 500) return;
    lastPublish = millis();

    if (hasValidHeight()) {
        updateCameraMeasurement(PersonStatus::Measured, getLastHeightCm(),
                                PERSON_DISTANCE_CM / 100.0f);
    } else {
        // "Present, height not measured yet": keeps a weight that arrives
        // before the height from being discarded.
        updateCameraMeasurement(PersonStatus::Partial);
    }
}

// When the person steps off (scale unload clears the weight), clear the old
// height too, so the next person never gets the previous person's BMI.
static void resetHeightWhenPersonLeaves(const MeasurementView& view) {
    static bool hadWeight = false;
    if (hadWeight && !view.hasWeight && hasValidHeight()) {
        Serial.println("[BMI] Weight removed -> clearing height for next person");
        requestHeightReset();
    }
    hadWeight = view.hasWeight;
}

static void printResultOnChange(const MeasurementView& view) {
    static bool printed = false;
    if (view.hasBMI && !printed) {
        Serial.println("========== HEIGHT / WEIGHT / BMI ==========");
        Serial.printf("Height = %.1f cm\n", view.heightCm);
        Serial.printf("Weight = %.2f kg\n", view.weightKg);
        Serial.printf("BMI    = %.2f\n", view.bmi);
        Serial.println("===========================================");
    }
    printed = view.hasBMI;
}

static void handleSerialLine(const char* line) {
    if (strcasecmp(line, "height reset") == 0 || strcasecmp(line, "reset") == 0) {
        requestHeightReset();
        return;
    }
    if (!handleScalePowerCommand(line)) {
        Serial.println("[CMD] Use: reset | power on | power off | power auto | power status");
    }
}

void setup() {
    Serial.begin(115200);
    const uint32_t serialStart = millis();
    while (!Serial && millis() - serialStart < 3000) delay(10);
    Serial.println("[BOOT] Height servo + scale + BMI");
    delay(500);  // LCD backpack supply settle.

    initScalePower();
    initDisplay();
    initHeightCamera();

    if (xTaskCreate(runHeightCameraTask, "HeightCamera", 6144, NULL, 2, NULL) != pdPASS) {
        Serial.println("[BOOT ERROR] Cannot create height camera task");
    }
    if (xTaskCreate(runScaleService, "ScaleBLEService", 10240, NULL, 1, NULL) != pdPASS) {
        Serial.println("[BOOT ERROR] Cannot create scale task");
    }
    Serial.println("[SYSTEM] Ready");
}

void loop() {
    publishHeight();

    const MeasurementView view = measurementView();
    resetHeightWhenPersonLeaves(view);
    printResultOnChange(view);
    updateDisplay();

    const unsigned long lastCamera = lastHeightCameraMessageMs();
    const bool cameraActive = lastCamera != 0 && millis() - lastCamera <= ACTIVITY_WINDOW_MS;
    updateScalePower(cameraActive || weightReceivedWithin(ACTIVITY_WINDOW_MS));

    static uint32_t lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 5000) {
        lastHeartbeat = millis();
        Serial.printf("[ALIVE] H=%s W=%s BMI=%s | lcd=%s scale=%s power=%s\n",
                      view.hasHeight ? String(view.heightCm, 1).c_str() : "--",
                      view.hasWeight ? String(view.weightKg, 2).c_str() : "--",
                      view.hasBMI ? String(view.bmi, 2).c_str() : "--",
                      displayStatus(), scaleServiceStatus(), scalePowerStatus());
    }

    static char line[32];
    static size_t lineLength = 0;
    while (Serial.available() > 0) {
        const char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (lineLength > 0) {
                line[lineLength] = '\0';
                handleSerialLine(line);
                lineLength = 0;
            }
        } else if (lineLength < sizeof(line) - 1) {
            line[lineLength++] = c;
        }
    }
    delay(10);
}
#endif
