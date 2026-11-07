#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"
#include "camera.h"
#include "measurements.h"
#include "camera_packet.h"

#if CAMERA_ENABLED
static HardwareSerial cameraSerial(CAMERA_UART);
static char frame[160];
static size_t used = 0;
static bool overflow = false;
static uint32_t lastByte = 0;
static uint32_t lastResult = 0;
static bool hasResult = false;

static void processFrame() {
  frame[used] = '\0';
  String value(frame);
  value.trim();
  if (!value.length()) return;
  Serial.printf("[Camera RX] %s\n", value.c_str());
  // Official extension uses label;probability. Some firmware uses class:label,percent.
  int split = value.lastIndexOf(';');
  bool percentFormat = false;
  if (split < 0 && value.startsWith("class:")) {
    value.remove(0, 6);
    split = value.lastIndexOf(',');
    percentFormat = true;
  }
  if (split <= 0) return;
  const String label = value.substring(0, split);
  const String score = value.substring(split + 1);
  char *end = nullptr;
  float confidence = strtof(score.c_str(), &end);
  if (end == score.c_str() || *end || !isfinite(confidence)) return;
  if (!percentFormat) confidence *= 100.0f;
  if (confidence < 0 || confidence > 100) return;
  lastResult = millis();
  hasResult = true;
  Serial.printf("[Camera] label=%s confidence=%.1f%%\n", label.c_str(), confidence);
  PersonStatus status;
  float heightCm, distanceM;
  if (decodeCameraLabel(label.c_str(), status, heightCm, distanceM)) {
    updateCameraMeasurement(status, heightCm, distanceM);
    if (status == PersonStatus::Measured) {
      Serial.printf("[Camera] Height=%.1f cm Distance=%.2f m\n", heightCm, distanceM);
    } else if (status == PersonStatus::None) {
      Serial.println("[Camera] No people detected");
    }
  }
  if (label.startsWith("HEIGHT_CM_")) {
    const String number = label.substring(10);
    const float height = strtof(number.c_str(), &end);
    if (end != number.c_str() && !*end && isfinite(height) && height >= 30 && height <= 250) {
      Serial.printf("[Camera] Estimated height: %.1f cm (fixed-position calibration)\n", height);
    }
  }
}
#endif

void initCamera() {
#if CAMERA_ENABLED
  cameraSerial.setRxBufferSize(512);
  cameraSerial.begin(CAMERA_BAUD, SERIAL_8N1, CAMERA_RX_PIN, CAMERA_TX_PIN);
  Serial.printf("[Camera] UART%d RX GPIO%d TX GPIO%d baud=%d; waiting for camera\n",
                CAMERA_UART, CAMERA_RX_PIN, CAMERA_TX_PIN, CAMERA_BAUD);
#endif
}

void pollCamera() {
#if CAMERA_ENABLED
  // Bound each poll so a noisy/floating RX or continuous stream cannot starve loop().
  for (size_t count = 0; count < 64 && cameraSerial.available(); ++count) {
    const char ch = cameraSerial.read();
    lastByte = millis();
    if (ch == '\n' || ch == '\r') {
      if (!overflow) processFrame();
      used = 0;
      overflow = false;
    } else if (!overflow) {
      if (used < sizeof(frame) - 1) frame[used++] = ch;
      else {
        used = 0;
        overflow = true;
        Serial.println("[Camera] Dropped oversized UART frame");
      }
    }
  }
  // Support firmware frames without a newline; discard partial/invalid frames.
  if ((used || overflow) && millis() - lastByte > 100) {
    if (!overflow) processFrame();
    used = 0;
    overflow = false;
  }
  if (hasResult && millis() - lastResult > 3000) {
    hasResult = false;
    updateCameraMeasurement(PersonStatus::None);
    Serial.println("[Camera] Result expired; waiting for fresh data");
  }
#endif
}
