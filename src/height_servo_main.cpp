// Servo-tilt height measurement (from HeightMeasure_YoloUNO/src/main.cpp).
// Compiled only by env:yolo_uno_height_servo; empty in every other env.
#ifdef HEIGHT_SERVO_MODE
#include <Arduino.h>

#include "height_camera.h"

void setup() {
    Serial.begin(115200);
    delay(1000);

    initHeightCamera();

    xTaskCreate(
        runHeightCameraTask,
        "HeightCamera",
        6144,
        NULL,
        2,
        NULL
    );

    Serial.println("[SYSTEM] Ready");
}

void loop() {
    delay(1000);
}
#endif
