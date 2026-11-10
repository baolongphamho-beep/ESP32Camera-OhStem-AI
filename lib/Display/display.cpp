#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "measurements.h"

static const char* lcdStatus = "disabled";

const char* displayStatus() { return lcdStatus; }

#if LCD_ENABLED
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#ifdef ARDUINO_YOLO_UNO
static_assert(SDA == 11 && SCL == 12, "YOLO UNO default I2C pinout mismatch");
#endif
#if LCD_SDA_PIN == CAMERA_RX_PIN || LCD_SDA_PIN == CAMERA_TX_PIN || LCD_SCL_PIN == CAMERA_RX_PIN || LCD_SCL_PIN == CAMERA_TX_PIN
#error "LCD I2C pins must not overlap camera UART pins"
#endif
static LiquidCrystal_I2C* lcd = nullptr;
static String previous[2];
static uint32_t lastLcdAttempt = 0;

static bool pinsAvailable(int sda, int scl) {
#if CAMERA_ENABLED
    if (sda == CAMERA_RX_PIN || sda == CAMERA_TX_PIN || scl == CAMERA_RX_PIN || scl == CAMERA_TX_PIN) return false;
#endif
#if SCALE_ENABLE_FORCE
    // Only the standard I2C pair is reserved when force sensors are enabled.
    if (sda != LCD_SDA_PIN || scl != LCD_SCL_PIN) return false;
#endif
    return true;
}

static bool lcdAddress(uint8_t address) {
    return LCD_I2C_ADDRESS ? address == LCD_I2C_ADDRESS :
           ((address >= 0x20 && address <= 0x27) || (address >= 0x38 && address <= 0x3f));
}

static void lcdLine(uint8_t row, String value) {
    value = value.substring(0, 16);
    while (value.length() < 16) value += ' ';
    if (value != previous[row]) {
        lcd->setCursor(0, row);
        lcd->print(value);
        previous[row] = value;
    }
}
#endif

void initDisplay() {
#if LCD_ENABLED
    if (lcd) return;
    lastLcdAttempt = millis();
    lcdStatus = "initializing";
    const int pairs[][2] = {
        {LCD_SDA_PIN, LCD_SCL_PIN},
#if LCD_AUTO_DETECT_PINS
        {6, 7},
        {8, 9}, // Automatically skipped when reserved by camera UART.
#endif
    };
    bool busStarted = false;
    for (const auto& pair : pairs) {
        const int sda = pair[0], scl = pair[1];
        if (!pinsAvailable(sda, scl)) {
            Serial.printf("[LCD] Skip SDA=%d SCL=%d: pins reserved\n", sda, scl);
            continue;
        }
        // begin() on an already running ESP32 bus does not change pins.
        Wire.end();
        if (!Wire.setPins(sda, scl) || !Wire.begin(sda, scl, 100000)) {
            Serial.printf("[LCD ERROR] Cannot start I2C SDA=%d SCL=%d\n", sda, scl);
            continue;
        }
        busStarted = true;
        Wire.setTimeOut(20);
        delay(50); // Match the working Arduino sketch's bus settling delay.
        Serial.printf("[LCD SCAN] SDA=%d SCL=%d (GPIO), 100kHz\n", sda, scl);
        uint8_t candidate = 0;
        unsigned candidates = 0;
        for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() != 0) continue;
            Serial.printf("[I2C] ACK address=0x%02x SDA=%d SCL=%d%s\n", addr, sda, scl,
                          lcdAddress(addr) ? " (LCD backpack candidate)" : "");
            if (lcdAddress(addr)) { candidate = addr; ++candidates; }
        }
        if (candidates > 1) {
            lcdStatus = "ambiguous-address";
            Serial.println("[LCD ERROR] Multiple backpack addresses; set LCD_I2C_ADDRESS in config.h");
            return;
        }
        if (!candidate) continue;
        lcd = new LiquidCrystal_I2C(candidate, 16, 2);
        lcd->init(); // Wire.begin() inside the library reuses the selected pins.
        lcd->backlight();
        lcdStatus = "ready";
        Serial.printf("[LCD] Address=0x%02x SDA=%d SCL=%d\n", candidate, sda, scl);
        lcdLine(0, "No people");
        lcdLine(1, "detected");
        return;
    }
    Wire.end();
    lcdStatus = busStarted ? "not-found" : "i2c-error";
    Serial.println("[LCD ERROR] LCD not found; retrying in 5 seconds. Compare [LCD SCAN]/[I2C] with Arduino sketch.");
#else
    Serial.println("[LCD] Disabled in this firmware. Upload yolo_uno_camera_lcd to enable LCD.");
#endif
}

void updateDisplay() {
#if LCD_ENABLED
    if (!lcd) {
        if (millis() - lastLcdAttempt >= 5000) initDisplay();
        return;
    }
    static uint32_t updated = 0;
    if (millis() - updated < 200) return;
    updated = millis();
    const MeasurementView view = measurementView();
    if (view.status == PersonStatus::None) {
        lcdLine(0, "No people");
        lcdLine(1, "detected");
    } else if (view.status == PersonStatus::Multiple) {
        lcdLine(0, "Multiple people");
        lcdLine(1, "One at a time");
    } else if ((millis() / 2500) % 2 == 0) {
        lcdLine(0, view.hasHeight ? "H:" + String(view.heightCm, 1) + "cm" : "H:-- (calib)");
        lcdLine(1, view.hasWeight ? "W:" + String(view.weightKg, 2) + "kg" + (view.weightStable ? " LOCK" : " ...") : "W:-- wait scale");
    } else if (view.status == PersonStatus::NeedsCalibration) {
        lcdLine(0, "Person detected");
        lcdLine(1, "Need calibration");
    } else if (view.status == PersonStatus::Partial) {
        lcdLine(0, "Person detected");
        lcdLine(1, "Full body needed");
    } else {
        lcdLine(0, view.hasHeight ? "D:" + String(view.distanceM, 2) + "m" : "D:--");
        lcdLine(1, view.hasBMI ? "BMI:" + String(view.bmi, 2) : "BMI:-- wait lock");
    }
#endif
}
