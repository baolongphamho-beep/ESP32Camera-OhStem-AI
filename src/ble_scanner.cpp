// Compiled as a standalone sketch only by env:yolo_uno_ble_scan.
#ifdef BLE_SCANNER_MODE
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <strings.h>
#include <esp_log.h>

// Standalone diagnostic setting. Keep in sync with EdgeConfig::scaleMac.
static const char* SCALE_MAC = "70:87:9e:ea:3a:e0";
static constexpr bool PRINT_HEX = false; // Enable only when inspecting packet bytes.

static void printHex(const std::string& payload) {
  Serial.printf("[SCALE HEX] len=%u |", (unsigned)payload.size());
  for (unsigned char byte : payload) Serial.printf(" %02X", byte);
  Serial.println();
}

static void printScaleWeight(const NimBLEAdvertisedDevice* device) {
  if (strcasecmp(device->getAddress().toString().c_str(), SCALE_MAC) != 0) return;

  const std::string payload = device->getServiceData(NimBLEUUID("181d"));
  if (payload.empty()) return;
  const auto* data = reinterpret_cast<const uint8_t*>(payload.data());
  const uint32_t now = millis();
  static bool diagnosticPrinted = false;
  static uint32_t lastDiagnosticMs = 0;
  const auto reportInvalid = [&](const char* message) {
    if (diagnosticPrinted && uint32_t(now - lastDiagnosticMs) < 1000) return;
    diagnosticPrinted = true;
    lastDiagnosticMs = now;
    Serial.println(message);
    printHex(payload);
  };

  // Xiaomi 0x181D: flags, little-endian uint16 weight, seven date/time bytes.
  // Do not apply this layout to 0x181B body-composition advertisements.
  if (payload.size() != 10) {
    reportInvalid("[SCALE] Unexpected 0x181D length; keep HEX for inspection.");
    return;
  }
  const uint8_t flags = data[0];
  const uint16_t raw = uint16_t(data[1]) | (uint16_t(data[2]) << 8);
  const bool stable = (flags & 0x20) != 0;
  const bool removed = (flags & 0x80) != 0;
  float kg;
  if (flags & 0x01) {
    kg = raw / 100.0f * 0.45359237f; // pounds -> kg
  } else if (flags & 0x10) {
    kg = raw / 100.0f * 0.6f; // Xiaomi jin unit -> kg
  } else if (flags & 0x02) {
    kg = raw / 200.0f;
  } else {
    reportInvalid("[SCALE] Unknown unit flags; weight not decoded.");
    return;
  }
  // Unload frames can carry an old weight and a stable bit. Never call it LOCK.
  const char* state = removed ? "REMOVED" : (raw == 0 ? "ZERO" : (stable ? "LOCK" : "MEASURING"));
  static bool printed = false;
  static uint32_t lastPrintMs = 0;
  static const char* lastState = nullptr;
  static uint16_t lastRaw = 0;
  static uint8_t lastFlags = 0;
  // Always report a phase change immediately. Otherwise print only changed
  // readings, at most once per second; duplicate packets stay silent.
  const bool changedState = !printed || state != lastState;
  const bool changedValue = raw != lastRaw || flags != lastFlags;
  if (!changedState && (!changedValue || uint32_t(now - lastPrintMs) < 1000)) return;
  printed = true;
  lastPrintMs = now;
  lastState = state;
  lastRaw = raw;
  lastFlags = flags;
  if (PRINT_HEX) printHex(payload);
  Serial.printf("[SCALE] Weight=%.2f kg | Stable=%d | Removed=%d | State=%s | Raw=%u | Flags=0x%02X\n",
                kg, stable, removed, state, raw, flags);
}

class ScannerCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    printScaleWeight(device);
  }
};

static ScannerCallbacks callbacks;

void setup() {
  Serial.begin(115200);
  const uint32_t started = millis();
  while (!Serial && millis() - started < 3000) delay(10);
  Serial.setDebugOutput(false);
  esp_log_level_set("*", ESP_LOG_ERROR); // Hide NimBLE discovery chatter.
  NimBLEDevice::init("YOLO UNO BLE Scanner");
  NimBLEScan* scanner = NimBLEDevice::getScan();
  scanner->setScanCallbacks(&callbacks, true);
  scanner->setDuplicateFilter(0); // Receive changing weight and unload packets.
  scanner->setActiveScan(true);
  scanner->setInterval(100);
  scanner->setWindow(100);
  Serial.println("[SCAN] Ready. Wake the scale by stepping on it; baud=115200.");
  Serial.printf("[SCAN] Decoding Xiaomi 0x181D weight for MAC=%s; compare with scale display.\n", SCALE_MAC);
}

void loop() {
  NimBLEScan* scanner = NimBLEDevice::getScan();
  if (!scanner->isScanning()) {
    scanner->clearResults();
    if (!scanner->start(10000)) Serial.println("[SCAN] Start failed; retrying.");
  }
  delay(1000);
}
#endif
