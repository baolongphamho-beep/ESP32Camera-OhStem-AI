#pragma once
#include "Wire.h"

class LiquidCrystal_I2C {
public:
    uint8_t address;
    uint8_t row = 0;
    std::string lines[2];
    LiquidCrystal_I2C(uint8_t addr, int, int) : address(addr) {}
    void init() { Wire.begin(); }
    void backlight() {}
    void setCursor(int, uint8_t target) { row = target; }
    void print(const String& text) { lines[row] = text.value; }
};
