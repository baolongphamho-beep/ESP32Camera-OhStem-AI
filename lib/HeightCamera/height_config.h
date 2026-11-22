#pragma once

#include <Arduino.h>

// Servo-tilt height measurement (from HeightMeasure_YoloUNO).
// Pins are raw GPIO numbers. Override them from platformio.ini
// (env:yolo_uno_height_servo) instead of editing this file.
// Note: on YOLO UNO, Grove D5/D6 are GPIO8/GPIO9; GPIO5/GPIO6 are A6/D3.
#ifndef HEIGHT_CAMERA_RX_PIN
#define HEIGHT_CAMERA_RX_PIN 5
#endif
#ifndef HEIGHT_CAMERA_TX_PIN
#define HEIGHT_CAMERA_TX_PIN 6
#endif
#ifndef HEIGHT_SERVO_PIN
#define HEIGHT_SERVO_PIN 2
#endif
#ifndef HEIGHT_PERSON_DISTANCE_CM
#define HEIGHT_PERSON_DISTANCE_CM 100.0f
#endif

constexpr int CAMERA_UART_NUM = 1;

constexpr int CAMERA_UART_RX_PIN = HEIGHT_CAMERA_RX_PIN;
constexpr int CAMERA_UART_TX_PIN = HEIGHT_CAMERA_TX_PIN;

constexpr uint32_t CAMERA_UART_BAUD = 115200;

constexpr int SERVO_PIN = HEIGHT_SERVO_PIN;

constexpr int SERVO_MIN_COMMAND_DEG = 20;
constexpr int SERVO_MAX_COMMAND_DEG = 160;

constexpr int SERVO_START_COMMAND_DEG = 90;

constexpr int SERVO_STEP_DEG = 1;

constexpr int SERVO_UP_SIGN = 1;

constexpr float SERVO_HORIZONTAL_COMMAND_DEG = 90.0f;

constexpr float SERVO_ANGLE_SCALE = 1.0f;

constexpr float PERSON_DISTANCE_CM = HEIGHT_PERSON_DISTANCE_CM;
