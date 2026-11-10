// Compiled as a standalone sketch only by env:yolo_uno_ble_scan.
#ifdef BLE_SCANNER_MODE
#include <Arduino.h>
#include <NimBLEDevice.h>

class ScannerCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    Serial.printf("[BLE] MAC=%s | Name=%s | RSSI=%d dBm\n",
                  device->getAddress().toString().c_str(),
                  device->getName().empty() ? "(no name)" : device->getName().c_str(),
                  device->getRSSI());
    const bool weightService = device->isAdvertisingService(NimBLEUUID("181d"));
    const bool bodyService = device->isAdvertisingService(NimBLEUUID("181b"));
    if (weightService || bodyService) {
      Serial.printf("[SCALE CANDIDATE] MAC=%s | WeightService=%d | BodyComposition=%d\n",
                    device->getAddress().toString().c_str(), weightService, bodyService);
    }
    // Include service data for nearby unnamed devices without hiding weak ones above.
    if (weightService || bodyService || device->getRSSI() >= -65) {
      Serial.printf("[BLE DETAILS] MAC=%s | %s\n",
                    device->getAddress().toString().c_str(), device->toString().c_str());
    }
  }
};

static ScannerCallbacks callbacks;

void setup() {
  Serial.begin(115200);
  const uint32_t started = millis();
  while (!Serial && millis() - started < 3000) delay(10);
  Serial.setDebugOutput(true);
  NimBLEDevice::init("YOLO UNO BLE Scanner");
  NimBLEScan* scanner = NimBLEDevice::getScan();
  scanner->setScanCallbacks(&callbacks, false);
  scanner->setActiveScan(true);
  scanner->setInterval(100);
  scanner->setWindow(100);
  Serial.println("[SCAN] Ready. Wake the scale by stepping on it; baud=115200.");
}

void loop() {
  NimBLEScan* scanner = NimBLEDevice::getScan();
  if (!scanner->isScanning()) {
    scanner->clearResults();
    Serial.println("[SCAN] Scanning all BLE devices for 10 seconds...");
    if (!scanner->start(10000)) Serial.println("[SCAN] Start failed; retrying.");
  }
  delay(1000);
}
#endif
