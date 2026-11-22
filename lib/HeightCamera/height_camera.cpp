#include <Arduino.h>
#include <ESP32Servo.h>
#include <HardwareSerial.h>
#include <math.h>

#include "height_camera.h"
#include "height_config.h"

static Servo tiltServo;

static HardwareSerial cameraSerial(
    CAMERA_UART_NUM
);

static float servoCommandDeg =
    SERVO_START_COMMAND_DEG;

static float thetaFootDeg = 0.0f;
static float thetaHeadDeg = 0.0f;

static float lastHeightCm = 0.0f;

static bool footCaptured = false;
static bool heightValid = false;

static String rxBuffer = "";

static unsigned long lastRxByteMs = 0;

static volatile unsigned long lastMessageMs = 0;

static volatile bool resetRequested = false;

static float degreeToRadian(
    float degree
) {
    return degree * PI / 180.0f;
}

static float clampServoCommand(
    float command
) {
    if (command < SERVO_MIN_COMMAND_DEG) {
        return SERVO_MIN_COMMAND_DEG;
    }

    if (command > SERVO_MAX_COMMAND_DEG) {
        return SERVO_MAX_COMMAND_DEG;
    }

    return command;
}

float getServoCommandDeg() {
    return servoCommandDeg;
}

float getOpticalAngleDeg() {
    return
        (
            servoCommandDeg
            -
            SERVO_HORIZONTAL_COMMAND_DEG
        )
        *
        SERVO_ANGLE_SCALE
        *
        SERVO_UP_SIGN;
}

float getFootAngleDeg() {
    return thetaFootDeg;
}

float getHeadAngleDeg() {
    return thetaHeadDeg;
}

float getLastHeightCm() {
    return lastHeightCm;
}

bool hasValidHeight() {
    return heightValid;
}

void requestHeightReset() {
    resetRequested = true;
}

unsigned long lastHeightCameraMessageMs() {
    return lastMessageMs;
}

static void setServoCommand(
    float command
) {
    servoCommandDeg =
        clampServoCommand(command);

    tiltServo.write(
        (int)roundf(servoCommandDeg)
    );

    Serial.printf(
        "[SERVO] command=%.1f deg | optical=%.3f deg\n",
        servoCommandDeg,
        getOpticalAngleDeg()
    );
}

static void moveCameraUp() {
    setServoCommand(
        servoCommandDeg
        +
        SERVO_STEP_DEG
        *
        SERVO_UP_SIGN
    );
}

static void moveCameraDown() {
    setServoCommand(
        servoCommandDeg
        -
        SERVO_STEP_DEG
        *
        SERVO_UP_SIGN
    );
}

static void resetMeasurement() {
    footCaptured = false;
    heightValid = false;

    thetaFootDeg = 0.0f;
    thetaHeadDeg = 0.0f;

    lastHeightCm = 0.0f;

    setServoCommand(
        SERVO_START_COMMAND_DEG
    );

    Serial.println(
        "[HEIGHT] Measurement reset"
    );
}

static void saveFootAngle() {
    thetaFootDeg =
        getOpticalAngleDeg();

    footCaptured = true;

    heightValid = false;

    Serial.println(
        "=============================="
    );

    Serial.printf(
        "[HEIGHT] FOOT ANGLE = %.3f deg\n",
        thetaFootDeg
    );

    Serial.println(
        "=============================="
    );
}

static void saveHeadAngleAndCalculate() {
    if (!footCaptured) {
        Serial.println(
            "[HEIGHT] ERROR: Save foot first"
        );

        return;
    }

    thetaHeadDeg =
        getOpticalAngleDeg();

    const float thetaFootRad =
        degreeToRadian(thetaFootDeg);

    const float thetaHeadRad =
        degreeToRadian(thetaHeadDeg);

    lastHeightCm =
        PERSON_DISTANCE_CM
        *
        (
            tanf(thetaHeadRad)
            -
            tanf(thetaFootRad)
        );

    heightValid =
        isfinite(lastHeightCm)
        &&
        lastHeightCm > 0.0f;

    Serial.println("");

    Serial.println(
        "========== RESULT =========="
    );

    Serial.printf(
        "D = %.2f cm\n",
        PERSON_DISTANCE_CM
    );

    Serial.printf(
        "Foot angle = %.3f deg\n",
        thetaFootDeg
    );

    Serial.printf(
        "Head angle = %.3f deg\n",
        thetaHeadDeg
    );

    if (heightValid) {
        Serial.printf(
            "HEIGHT = %.2f cm\n",
            lastHeightCm
        );
    } else {
        Serial.println(
            "HEIGHT = INVALID"
        );
    }

    Serial.println(
        "============================"
    );

    Serial.println("");
}

static void processCameraMessage(
    String message
) {
    message.trim();

    message.toUpperCase();

    if (message.length() == 0) {
        return;
    }

    lastMessageMs = millis();

    Serial.printf(
        "[CAMERA UART] %s\n",
        message.c_str()
    );

    // FOOT_* and HEAD_SEARCH_UP are sent by pedestrian_detector.py
    // (automatic mode); MANUAL_* / MARK_* by manual_height_test.py.
    if (
        message.indexOf("MANUAL_UP") >= 0
        ||
        message.indexOf("HEAD_ABOVE") >= 0
        ||
        message.indexOf("HEAD_SEARCH_UP") >= 0
        ||
        message.indexOf("FOOT_ABOVE") >= 0
    ) {
        moveCameraUp();
        return;
    }

    if (
        message.indexOf("MANUAL_DOWN") >= 0
        ||
        message.indexOf("HEAD_BELOW") >= 0
        ||
        message.indexOf("FOOT_BELOW") >= 0
    ) {
        moveCameraDown();
        return;
    }

    if (
        message.indexOf("MARK_FOOT") >= 0
        ||
        message.indexOf("FOOT_CENTER") >= 0
    ) {
        saveFootAngle();
        return;
    }

    if (
        message.indexOf("MARK_HEAD") >= 0
        ||
        message.indexOf("HEAD_CENTER") >= 0
    ) {
        saveHeadAngleAndCalculate();
        return;
    }

    if (
        message.indexOf("RESET_HEIGHT") >= 0
    ) {
        resetMeasurement();
        return;
    }
}

void initHeightCamera() {
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);

    tiltServo.setPeriodHertz(50);

    tiltServo.attach(
        SERVO_PIN,
        500,
        2500
    );

    setServoCommand(
        SERVO_START_COMMAND_DEG
    );

    cameraSerial.begin(
        CAMERA_UART_BAUD,
        SERIAL_8N1,
        CAMERA_UART_RX_PIN,
        CAMERA_UART_TX_PIN
    );

    Serial.println("");

    Serial.println(
        "================================"
    );

    Serial.println(
        "HEIGHT MEASUREMENT SYSTEM"
    );

    Serial.println(
        "================================"
    );

    Serial.printf(
        "Camera UART RX=%d TX=%d\n",
        CAMERA_UART_RX_PIN,
        CAMERA_UART_TX_PIN
    );

    Serial.printf(
        "Servo pin=%d\n",
        SERVO_PIN
    );

    Serial.printf(
        "Person distance D=%.2f cm\n",
        PERSON_DISTANCE_CM
    );

    Serial.printf(
        "Horizontal command=%.2f\n",
        SERVO_HORIZONTAL_COMMAND_DEG
    );

    Serial.printf(
        "Angle scale=%.4f\n",
        SERVO_ANGLE_SCALE
    );

    Serial.println(
        "================================"
    );
}

void runHeightCameraTask(
    void *pvParameters
) {
    while (true) {
        if (resetRequested) {
            resetRequested = false;
            resetMeasurement();
        }

        while (
            cameraSerial.available() > 0
        ) {
            const char character =
                (char)cameraSerial.read();

            lastRxByteMs = millis();

            if (
                character == '\n'
                ||
                character == '\r'
            ) {
                if (
                    rxBuffer.length() > 0
                ) {
                    processCameraMessage(
                        rxBuffer
                    );

                    rxBuffer = "";
                }
            } else {
                rxBuffer += character;

                if (
                    rxBuffer.length() > 200
                ) {
                    rxBuffer = "";
                }
            }
        }

        if (
            rxBuffer.length() > 0
            &&
            millis() - lastRxByteMs > 50
        ) {
            processCameraMessage(
                rxBuffer
            );

            rxBuffer = "";
        }

        vTaskDelay(
            pdMS_TO_TICKS(10)
        );
    }
}
