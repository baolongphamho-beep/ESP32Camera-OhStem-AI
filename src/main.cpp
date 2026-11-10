#include <Arduino.h>
#include "scale.h"
#include "config.h"
#include "camera.h"
#include "display.h"

void TaskLEDControl(void *pvParameters) {
  pinMode(PIN_LED, OUTPUT); // Initialize LED pin
  int ledState = 0;
  while(1) {
    
    if (ledState == 0) {
      digitalWrite(PIN_LED, HIGH); // Turn ON LED
    } else {
      digitalWrite(PIN_LED, LOW); // Turn OFF LED
    }
    ledState = 1 - ledState;
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin (115200);
  // Allow USB serial to connect, without blocking standalone operation.
  const uint32_t serialStart = millis();
  while (!Serial && millis() - serialStart < 3000) {
    delay(10);
  }
  Serial.setDebugOutput(true);
  Serial.println("[BOOT] YOLO UNO started; Serial baud = 115200");
  delay(500); // Allow the LCD/backpack supply to settle as in AI_Vision_camera.ino.
  initDisplay();
  initCamera();
  
  const BaseType_t scaleTask = xTaskCreate(runScaleService, "ScaleBLEService", 10240, NULL, 1, NULL);
  Serial.printf("[BOOT] Scale task: %s; free heap=%lu\n",
                scaleTask == pdPASS ? "created" : "FAILED",
                (unsigned long)ESP.getFreeHeap());
  if (xTaskCreate(TaskLEDControl, "LEDControl", 1024, NULL, 2, NULL) != pdPASS) {
    Serial.println("[BOOT ERROR] Cannot create LED task");
  }
}

void loop() {
  static uint32_t lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 5000) {
    lastHeartbeat = millis();
    Serial.printf("[ALIVE] uptime=%lu s; lcd=%s; scale=%s; free heap=%lu\n",
                (unsigned long)(millis() / 1000), displayStatus(), scaleServiceStatus(),
                (unsigned long)ESP.getFreeHeap());
  }
  pollCamera();
  updateDisplay();
  delay(10);
}

