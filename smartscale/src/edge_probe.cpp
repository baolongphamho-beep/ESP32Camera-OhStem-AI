#ifdef EDGE_PROBE_MODE
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp32-hal-psram.h>

static void memoryReport() {
    Serial.printf("[EDGE PROBE] Flash=%lu bytes; PSRAM=%lu bytes; free PSRAM=%lu; largest PSRAM block=%lu; free internal=%lu\n",
                  (unsigned long)ESP.getFlashChipSize(), (unsigned long)ESP.getPsramSize(),
                  (unsigned long)ESP.getFreePsram(),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

void setup() {
    Serial.begin(115200);
    const uint32_t start = millis();
    while (!Serial && millis() - start < 3000) delay(10);
    Serial.setDebugOutput(true);
    Serial.println("[EDGE PROBE] YOLO UNO memory check; no inference, no flash/NVS writes");
    memoryReport();
    if (!psramFound()) {
        Serial.println("[EDGE PROBE ERROR] PSRAM unavailable: verify module and memory wiring/configuration");
        return;
    }
    // Verify a modest RGB inference input allocation, not total model feasibility.
    constexpr size_t inputBytes = 224 * 224 * 3;
    uint8_t* input = static_cast<uint8_t*>(heap_caps_malloc(inputBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (input) {
        memset(input, 0, inputBytes);
        Serial.printf("[EDGE PROBE] 224x224 RGB buffer allocation OK (%u bytes); model activations not tested\n", unsigned(inputBytes));
        heap_caps_free(input);
    } else {
        Serial.println("[EDGE PROBE ERROR] Cannot allocate 224x224 RGB buffer");
    }
}

void loop() {
    memoryReport();
    delay(5000);
}
#endif
